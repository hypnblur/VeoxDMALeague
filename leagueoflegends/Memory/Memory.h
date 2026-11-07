#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <memory>

#include "../Include/vmmdll.h"

#ifndef LOG
#define LOG(...) printf(__VA_ARGS__)
#endif

class Memory
{
private:
	struct LibModules
	{
		HMODULE VMM = nullptr;
		HMODULE FTD3XX = nullptr;
		HMODULE LEECHCORE = nullptr;
	};

	static inline LibModules modules{ };

	struct CurrentProcessInformation
	{
		int PID = 0;
		size_t base_address = 0;
		size_t base_size = 0;
		std::string process_name = "";
	};
	std::unordered_map<std::wstring, ULONG64> Modules;

	static inline BOOLEAN DMA_INITIALIZED = FALSE;
	static inline BOOLEAN PROCESS_INITIALIZED = FALSE;

	bool DumpMemoryMap(bool debug = false);
	bool SetFPGA();

public:
	static inline CurrentProcessInformation current_process{ };

	Memory();
	~Memory();

	bool Init(std::string process_name, bool memMap = true, bool debug = false);
	bool Reattach();

	DWORD GetPidFromName(std::string process_name);
	std::vector<int> GetPidListFromName(std::string process_name);
	std::vector<std::string> GetModuleList(std::string process_name);
	VMMDLL_PROCESS_INFORMATION GetProcessInformation();

	size_t GetBaseAddress(std::string module_name);
	size_t GetBaseSize(std::string module_name);

	uintptr_t GetExportTableAddress(std::string import, std::string process, std::string module);
	uintptr_t GetImportTableAddress(std::string import, std::string process, std::string module);

	bool FixCr3();
	bool DumpMemory(uintptr_t address, std::string path);
	uint64_t FindSignature(const char* signature, uint64_t range_start, uint64_t range_end, int PID = 0);

	bool Write(uintptr_t address, void* buffer, size_t size) const;
	bool Write(uintptr_t address, void* buffer, size_t size, int pid) const;

	template <typename T>
	bool Write(void* address, T value)
	{
		return Write(reinterpret_cast<uintptr_t>(address), &value, sizeof(T));
	}

	template <typename T>
	bool Write(uintptr_t address, T value)
	{
		return Write(address, &value, sizeof(T));
	}

	bool Read(uintptr_t address, void* buffer, size_t size) const;
	bool Read(uintptr_t address, void* buffer, size_t size, int pid) const;

	template <typename T>
	T Read(void* address)
	{
		T buffer{ };
		memset(&buffer, 0, sizeof(T));
		Read(reinterpret_cast<uint64_t>(address), reinterpret_cast<void*>(&buffer), sizeof(T));
		return buffer;
	}

	template <typename T>
	T Read(uint64_t address)
	{
		return Read<T>(reinterpret_cast<void*>(address));
	}

	template <typename T>
	T Read(void* address, int pid)
	{
		T buffer{ };
		memset(&buffer, 0, sizeof(T));
		Read(reinterpret_cast<uint64_t>(address), reinterpret_cast<void*>(&buffer), sizeof(T), pid);
		return buffer;
	}

	template <typename T>
	T Read(uint64_t address, int pid)
	{
		return Read<T>(reinterpret_cast<void*>(address), pid);
	}

	bool ReadString(uint64_t address, char* buffer, size_t maxLen) const
	{
		return Read(address, buffer, maxLen);
	}

	VMMDLL_SCATTER_HANDLE CreateScatterHandle();
	VMMDLL_SCATTER_HANDLE CreateScatterHandle(int pid);
	void CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle);

	bool AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size);
	void AddScatterWriteRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size);

	template <typename T>
	bool AddScatterWriteRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t addr, T value) const
	{
		if (!(addr > 0x2000000 && addr < 0x7FFFFFFFFFFF))
			return false;
		if (!VMMDLL_Scatter_PrepareWrite(handle, addr, reinterpret_cast<PBYTE>(&value), sizeof(value)))
		{
			LOG("failed to prepare scatter write at 0x%p\n", addr);
			return false;
		}
		return true;
	}

	void ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid = 0);
	void ExecuteWriteScatter(VMMDLL_SCATTER_HANDLE handle, int pid = 0);

	VMM_HANDLE vHandle;
};

inline Memory TargetProcess;
