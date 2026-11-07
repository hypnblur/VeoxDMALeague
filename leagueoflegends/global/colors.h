#pragma once
#include "../imgui/imgui.h"

#define COLOR_WHITE ImVec4(1.0f, 1.0f, 1.0f, 1.0f)

namespace colors {
    namespace child {
        inline ImVec4 border      = ImVec4(0.20f, 0.20f, 0.24f, 0.50f);
        inline ImVec4 border_line = ImVec4(0.20f, 0.20f, 0.24f, 0.35f);
        inline ImVec4 text        = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
        inline ImVec4 child       = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    }
    namespace keybind {
        inline ImVec4 key_text      = ImVec4(0.55f, 0.55f, 0.62f, 1.00f);
        inline ImVec4 color_rect    = ImVec4(0.35f, 0.56f, 1.00f, 1.00f);
        inline ImVec4 color_outline = ImVec4(0.25f, 0.40f, 0.75f, 1.00f);
    }
    namespace utility {
        inline ImVec4 circle_user = ImVec4(0.35f, 0.56f, 1.00f, 1.00f);
    }
    namespace button {
        inline ImVec4 text             = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
        inline ImVec4 text_active      = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        inline ImVec4 background       = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
        inline ImVec4 background_hov   = ImVec4(0.20f, 0.30f, 0.60f, 0.25f);
        inline ImVec4 background_active= ImVec4(0.25f, 0.40f, 0.75f, 1.00f);
    }
    namespace scroll {
        inline ImVec4 background = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
        inline ImVec4 scroll     = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    }
    namespace checkbox {
        inline ImVec4 text            = ImVec4(0.55f, 0.55f, 0.62f, 1.00f);
        inline ImVec4 text_hov        = ImVec4(0.75f, 0.75f, 0.80f, 1.00f);
        inline ImVec4 text_active     = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
        inline ImVec4 circle_active   = ImVec4(0.35f, 0.56f, 1.00f, 1.00f);
        inline ImVec4 circle_inactive = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
        inline ImVec4 background      = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
    }
    namespace background {
        inline ImVec4 border = ImVec4(0.20f, 0.20f, 0.24f, 0.50f);
    }
    namespace combo {
        inline ImVec4 background  = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
        inline ImVec4 circle      = ImVec4(0.55f, 0.55f, 0.62f, 1.00f);
        namespace selectable {
            inline ImVec4 text        = ImVec4(0.55f, 0.55f, 0.62f, 1.00f);
            inline ImVec4 text_hov    = ImVec4(0.75f, 0.75f, 0.80f, 1.00f);
            inline ImVec4 text_active = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
            inline ImVec4 mark_active = ImVec4(0.35f, 0.56f, 1.00f, 1.00f);
        }
    }
    namespace slider {
        inline ImVec4 text            = ImVec4(0.55f, 0.55f, 0.62f, 1.00f);
        inline ImVec4 text_hov        = ImVec4(0.75f, 0.75f, 0.80f, 1.00f);
        inline ImVec4 text_active     = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
        inline ImVec4 circle_active   = ImVec4(0.35f, 0.56f, 1.00f, 1.00f);
        inline ImVec4 circle_inactive = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
        inline ImVec4 background      = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
        inline ImVec4 gradient_start  = ImVec4(0.35f, 0.56f, 1.00f, 1.00f);
        inline ImVec4 gradient_end    = ImVec4(0.25f, 0.40f, 0.75f, 1.00f);
    }
    namespace picker {
        inline ImVec4 text        = ImVec4(0.55f, 0.55f, 0.62f, 1.00f);
        inline ImVec4 text_hov    = ImVec4(0.75f, 0.75f, 0.80f, 1.00f);
        inline ImVec4 text_active = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
        inline ImVec4 background  = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
        inline ImVec4 shadow      = ImVec4(0.00f, 0.00f, 0.00f, 0.30f);
        inline ImVec4 border      = ImVec4(0.20f, 0.20f, 0.24f, 0.50f);
    }
}
