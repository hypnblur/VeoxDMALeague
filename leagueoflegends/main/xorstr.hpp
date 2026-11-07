/*
 * xorstr.hpp - compile-time string obfuscation (scalar triple-mix variant)
 * Encrypt: E = (P ^ K) + rotl64(K, idx*11+5)
 * Decrypt: P = (E - rotl64(K, idx*11+5)) ^ K
 * Key generation: MurmurHash3 finalization mix (instead of plain FNV)
 * Each 64-bit block uses a different rotation amount, so the emitted code
 * shows a ROL+SUB+XOR mix pattern.
 */

#ifndef JM_XORSTR_HPP
#define JM_XORSTR_HPP

#include <cstdint>
#include <cstddef>
#include <utility>
#include <type_traits>

#define xorstr(str) ::jm::xor_string([]() { return str; }, std::integral_constant<std::size_t, sizeof(str) / sizeof(*str)>{}, std::make_index_sequence<::jm::detail::_buffer_size<sizeof(str)>()>{})
#define xorstr_(str) xorstr(str).crypt_get()

#ifdef _MSC_VER
#define XORSTR_FORCEINLINE __forceinline
#else
#define XORSTR_FORCEINLINE __attribute__((always_inline)) inline
#endif

namespace jm {

    namespace detail {

        template<std::size_t Size>
        XORSTR_FORCEINLINE constexpr std::size_t _buffer_size()
        {
            return ((Size / 16) + (Size % 16 != 0)) * 2;
        }

        // Compile-time/runtime rotate-left (rotation amount masked to 0-63)
        XORSTR_FORCEINLINE constexpr std::uint64_t rotl64(std::uint64_t v, int r) noexcept
        {
            r &= 63;
            return r == 0 ? v : (v << r) | (v >> (64 - r));
        }

        // Key generation - MurmurHash3 finalization mix (instead of FNV 16777619).
        // Different polynomials and shift amounts produce different code
        // sequences.
        template<std::uint32_t Seed>
        XORSTR_FORCEINLINE constexpr std::uint32_t key4() noexcept
        {
            std::uint32_t h = Seed ^ 0xDEADC0DEu;
            for (char c : __TIME__) {
                h ^= static_cast<std::uint32_t>(static_cast<unsigned char>(c));
                h *= 0x85EBCA6Bu;
                h ^= h >> 13;
                h *= 0xC2B2AE35u;
                h ^= h >> 16;
            }
            h ^= h >> 16;
            h *= 0x85EBCA6Bu;
            h ^= h >> 13;
            h *= 0xC2B2AE35u;
            h ^= h >> 16;
            return h;
        }

        template<std::size_t S>
        XORSTR_FORCEINLINE constexpr std::uint64_t key8()
        {
            constexpr auto lo = key4<2246822519u + static_cast<std::uint32_t>(S)>();
            constexpr auto hi = key4<lo ^ 0xA5A5A5A5u>();
            return (static_cast<std::uint64_t>(hi) << 32) | lo;
        }

        // Encrypt: pack up to 8 bytes of the string and compute
        // E = (P ^ K) + rotl64(K, idx*11+5) - adds a rotate and an add to the
        // plain XOR of the classic load_xored_str8.
        template<std::size_t N, class CharT>
        XORSTR_FORCEINLINE constexpr std::uint64_t
        load_enc_str8(std::uint64_t key, std::size_t idx, const CharT* str) noexcept
        {
            using cast_type = typename std::make_unsigned<CharT>::type;
            constexpr auto value_size = sizeof(CharT);
            constexpr auto idx_offset = 8 / value_size;

            std::uint64_t plain = 0;
            for (std::size_t i = 0; i < idx_offset && i + idx * idx_offset < N; ++i)
                plain |= (std::uint64_t{ static_cast<cast_type>(str[i + idx * idx_offset]) }
                          << ((i % idx_offset) * 8 * value_size));

            const std::uint64_t rot_k = rotl64(key, static_cast<int>(idx * 11 + 5));
            return (plain ^ key) + rot_k;
        }

        // forces compiler to use registers instead of stuffing constants in rdata
        XORSTR_FORCEINLINE std::uint64_t load_from_reg(std::uint64_t value) noexcept
        {
#if defined(__clang__) || defined(__GNUC__)
            asm("" : "=r"(value) : "0"(value) :);
            return value;
#else
            volatile std::uint64_t reg = value;
            return reg;
#endif
        }

    } // namespace detail

    template<class CharT, std::size_t Size, class Keys, class Indices>
    class xor_string;

    template<class CharT, std::size_t Size, std::uint64_t... Keys, std::size_t... Indices>
    class xor_string<CharT, Size, std::integer_sequence<std::uint64_t, Keys...>, std::index_sequence<Indices...>> {
        constexpr static inline std::uint64_t alignment = 16;

        alignas(alignment) std::uint64_t _storage[sizeof...(Keys)];

    public:
        using value_type    = CharT;
        using size_type     = std::size_t;
        using pointer       = CharT*;
        using const_pointer = const CharT*;

        template<class L>
        XORSTR_FORCEINLINE xor_string(L l, std::integral_constant<std::size_t, Size>, std::index_sequence<Indices...>) noexcept
            : _storage{ ::jm::detail::load_from_reg((std::integral_constant<std::uint64_t, detail::load_enc_str8<Size>(Keys, Indices, l())>::value))... }
        {}

        XORSTR_FORCEINLINE constexpr size_type size() const noexcept
        {
            return Size - 1;
        }

        XORSTR_FORCEINLINE void crypt() noexcept
        {
            // Decrypt: P = (E - rotl64(K, idx*11+5)) ^ K
            // Each block's rotation amount Indices*11+5 is known at compile
            // time, producing a different ROL+SUB+XOR instruction sequence.
            alignas(alignment) std::uint64_t keys[]{ ::jm::detail::load_from_reg(Keys)... };
            ((void)(_storage[Indices] =
                (_storage[Indices] - ::jm::detail::rotl64(keys[Indices], static_cast<int>(Indices * 11 + 5)))
                ^ keys[Indices]), ...);
        }

        XORSTR_FORCEINLINE const_pointer get() const noexcept
        {
            return reinterpret_cast<const_pointer>(_storage);
        }

        XORSTR_FORCEINLINE pointer get() noexcept
        {
            return reinterpret_cast<pointer>(_storage);
        }

        XORSTR_FORCEINLINE pointer crypt_get() noexcept
        {
            // Decrypt: P = (E - rotl64(K, idx*11+5)) ^ K
            alignas(alignment) std::uint64_t keys[]{ ::jm::detail::load_from_reg(Keys)... };
            ((void)(_storage[Indices] =
                (_storage[Indices] - ::jm::detail::rotl64(keys[Indices], static_cast<int>(Indices * 11 + 5)))
                ^ keys[Indices]), ...);
            return reinterpret_cast<pointer>(_storage);
        }
    };

    template<class L, std::size_t Size, std::size_t... Indices>
    xor_string(L l, std::integral_constant<std::size_t, Size>, std::index_sequence<Indices...>) -> xor_string<
                std::remove_const_t<std::remove_reference_t<decltype(l()[0])>>,
                Size,
                std::integer_sequence<std::uint64_t, detail::key8<Indices>()...>,
                std::index_sequence<Indices...>>;

} // namespace jm

#endif // include guard
