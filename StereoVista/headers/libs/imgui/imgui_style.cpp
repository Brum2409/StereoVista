#include "IconsFontAwesome5.h"
#include "imgui_sytle.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>


// Global instances
ImGuiFonts g_Fonts;
GuiScaleSettings g_GuiScale;
CustomStyleColors g_StyleColors;
int g_currentTheme = GUI_THEME_MODERN_DARK;

// ===========================================================================
// Color themes
// ===========================================================================

// A theme is described by a compact palette. The four background levels are
// ordered from the primary surface outward: for DARK themes lvl0 is the
// darkest (title bars / scrollbar track) and lvl3 the lightest (frames,
// buttons, borders); for LIGHT themes lvl0 is the lightest (window) and lvl3
// the darkest (strong borders / active grabs). The accent drives all the
// interactive highlights; status colors feed toasts and the FPS overlay.
struct ThemePalette {
  const char *name;
  bool isDark;
  ImVec4 lvl0, lvl1, lvl2, lvl3;
  ImVec4 textPrimary, textSecondary, textDisabled;
  ImVec4 accent, accentHover, accentActive;
  ImVec4 success, warning, danger, info;
};

// 0xRRGGBB -> ImVec4 helper for authoring palettes from hex codes.
static ImVec4 Hex(unsigned int rgb, float a = 1.0f) {
  return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f,
                (rgb & 0xFF) / 255.0f, a);
}

static const ThemePalette &GetPalette(int theme) {
  // Function-local static: built once, on first use. Keep this array in the
  // same order as the GuiTheme enum.
  static const ThemePalette palettes[GUI_THEME_COUNT] = {
      // GUI_THEME_MODERN_DARK -- the original cool indigo dark theme (exact
      // float values preserved so the default look is unchanged).
      {"Modern Dark", true,
       ImVec4(0.085f, 0.090f, 0.105f, 1.00f), ImVec4(0.115f, 0.122f, 0.140f, 1.00f),
       ImVec4(0.155f, 0.165f, 0.190f, 1.00f), ImVec4(0.205f, 0.217f, 0.245f, 1.00f),
       ImVec4(0.95f, 0.96f, 0.98f, 1.00f), ImVec4(0.65f, 0.68f, 0.72f, 1.00f),
       ImVec4(0.46f, 0.49f, 0.55f, 1.00f),
       ImVec4(0.33f, 0.56f, 1.00f, 1.00f), ImVec4(0.45f, 0.66f, 1.00f, 1.00f),
       ImVec4(0.25f, 0.46f, 0.92f, 1.00f),
       ImVec4(0.26f, 0.73f, 0.42f, 1.00f), ImVec4(0.96f, 0.69f, 0.13f, 1.00f),
       ImVec4(0.86f, 0.28f, 0.29f, 1.00f), ImVec4(0.20f, 0.65f, 0.87f, 1.00f)},

      // GUI_THEME_MODERN_LIGHT -- the original clean light theme (preserved).
      {"Modern Light", false,
       ImVec4(0.98f, 0.98f, 0.99f, 1.00f), ImVec4(0.94f, 0.95f, 0.96f, 1.00f),
       ImVec4(0.88f, 0.89f, 0.91f, 1.00f), ImVec4(0.82f, 0.84f, 0.86f, 1.00f),
       ImVec4(0.10f, 0.10f, 0.12f, 1.00f), ImVec4(0.40f, 0.42f, 0.46f, 1.00f),
       ImVec4(0.60f, 0.62f, 0.66f, 1.00f),
       ImVec4(0.26f, 0.50f, 0.98f, 1.00f), ImVec4(0.36f, 0.60f, 1.00f, 1.00f),
       ImVec4(0.20f, 0.42f, 0.90f, 1.00f),
       ImVec4(0.20f, 0.70f, 0.35f, 1.00f), ImVec4(0.90f, 0.65f, 0.10f, 1.00f),
       ImVec4(0.85f, 0.25f, 0.25f, 1.00f), ImVec4(0.15f, 0.60f, 0.85f, 1.00f)},

      // GUI_THEME_SCHNEIDER_DIGITAL -- the Schneider Digital brand: clean white
      // surfaces, near-black text and the signature golden-yellow accent (matched
      // to the logo / call-to-action yellow on schneider-digital.com).
      {"Schneider Digital", false,
       Hex(0xFCFCFC), Hex(0xEFF0F1), Hex(0xDDDFE2), Hex(0xC0C3C7),
       Hex(0x1B1B1B), Hex(0x565A5E), Hex(0x9AA0A6),
       Hex(0xF3C218), Hex(0xFFD23D), Hex(0xD9A50A),
       Hex(0x2FA15A), Hex(0xE0900F), Hex(0xD63A3A), Hex(0x1F8FCF)},

      // GUI_THEME_NORD -- the popular muted arctic blue-gray palette; very easy
      // on the eyes for long sessions.
      {"Nord", true,
       Hex(0x2E3440), Hex(0x343C4A), Hex(0x3B4252), Hex(0x4C566A),
       Hex(0xECEFF4), Hex(0xC2CAD8), Hex(0x808C9E),
       Hex(0x88C0D0), Hex(0x9FD3E0), Hex(0x5E81AC),
       Hex(0xA3BE8C), Hex(0xEBCB8B), Hex(0xBF616A), Hex(0x81A1C1)},

      // GUI_THEME_FOREST -- warm olive/green with a cream foreground.
      {"Forest", true,
       Hex(0x141711), Hex(0x1B211A), Hex(0x252D1F), Hex(0x3A4630),
       Hex(0xEBD5AB), Hex(0xA8B58C), Hex(0x6E7A5A),
       Hex(0x8BAE66), Hex(0xA6C683), Hex(0x628141),
       Hex(0x6FBF73), Hex(0xE0A93B), Hex(0xD9694E), Hex(0x6FA8C9)},

      // GUI_THEME_LAGOON -- deep teal with a mint accent and cream text.
      {"Lagoon", true,
       Hex(0x122420), Hex(0x1A312C), Hex(0x214139), Hex(0x2F5A4F),
       Hex(0xFFF4E1), Hex(0xA7C5BB), Hex(0x6F8A82),
       Hex(0x89D7B7), Hex(0xA6E6CC), Hex(0x428475),
       Hex(0x5FC79A), Hex(0xE8B85C), Hex(0xE07A6A), Hex(0x5FB0C9)},

      // GUI_THEME_AMETHYST -- neutral near-black with a soft violet accent.
      {"Amethyst", true,
       Hex(0x15131C), Hex(0x1C1925), Hex(0x272234), Hex(0x352E47),
       Hex(0xECE9F2), Hex(0xA79FB8), Hex(0x6E667E),
       Hex(0xA579F0), Hex(0xB994F5), Hex(0x8A5BE0),
       Hex(0x56C596), Hex(0xE9B44C), Hex(0xE5616E), Hex(0x6FA8DC)},
  };
  if (theme < 0 || theme >= GUI_THEME_COUNT)
    theme = GUI_THEME_MODERN_DARK;
  return palettes[theme];
}

int GetGuiThemeCount() { return GUI_THEME_COUNT; }
const char *GetGuiThemeName(int theme) { return GetPalette(theme).name; }
bool IsGuiThemeDark(int theme) { return GetPalette(theme).isDark; }

int GetGuiThemeSwatches(int theme, ImVec4 *out, int maxColors) {
  const ThemePalette &p = GetPalette(theme);
  const ImVec4 colors[4] = {p.lvl1, p.accent, p.textPrimary, p.lvl3};
  int n = maxColors < 4 ? maxColors : 4;
  for (int i = 0; i < n; ++i)
    out[i] = colors[i];
  return n;
}

bool InitializeImGuiWithFonts(GLFWwindow *window, bool isDarkTheme) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;

  // Enable additional ImGui features
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  // --- Docking-branch features -------------------------------------------
  // Docking: let the floating tool/settings windows be docked together into
  // tab groups or split layouts.
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  // Multi-viewport: let windows (Settings, tool panels, ...) be dragged out of
  // the main application window and live as independent OS windows.
  // NOTE: These flags must be set BEFORE the GLFW/OpenGL backends are
  // initialized below -- the backends only create their platform/viewport
  // interface when ImGuiConfigFlags_ViewportsEnable is already set at init.
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

  io.ConfigWindowsMoveFromTitleBarOnly = true;

  // Initialize GLFW and OpenGL backends
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  // Configure style
  SetupImGuiStyle(isDarkTheme, 1.0f);

  // Font configuration with multiple options
  ImFontConfig fontConfig;
  fontConfig.OversampleH = 3;
  fontConfig.OversampleV = 3;
  fontConfig.PixelSnapH = true;
  fontConfig.RasterizerMultiply = 1.2f;

  // Modern font paths to try (in order of preference)
  const char *fontPaths[] = {// Windows fonts
                             "C:\\Windows\\Fonts\\SegoeUI.ttf",
                             "C:\\Windows\\Fonts\\Roboto-Regular.ttf",
                             "C:\\Windows\\Fonts\\Calibri.ttf",
                             "C:\\Windows\\Fonts\\Arial.ttf",
                             // Common installed fonts
                             "C:\\Program Files\\Microsoft\\Fonts\\SegoeUI.ttf",
                             // Fallback
                             "C:\\Windows\\Fonts\\Tahoma.ttf"};

  const char *boldFontPaths[] = {
      "C:\\Windows\\Fonts\\SegoeUI-Bold.ttf",
      "C:\\Windows\\Fonts\\Roboto-Bold.ttf", "C:\\Windows\\Fonts\\Calibrib.ttf",
      "C:\\Windows\\Fonts\\Arialbd.ttf", "C:\\Windows\\Fonts\\Tahomabd.ttf"};

  const char *monoFontPaths[] = {"C:\\Windows\\Fonts\\CascadiaCode.ttf",
                                 "C:\\Windows\\Fonts\\Consolas.ttf",
                                 "C:\\Windows\\Fonts\\CourierNew.ttf",
                                 "C:\\Windows\\Fonts\\LucidaConsole.ttf"};

  bool fontLoaded = false;
  float scale = g_GuiScale.currentScale;

  // Define font sizes
  float regularSize = 16.0f * scale;
  float boldSize = 16.0f * scale;
  float headerSize = 20.0f * scale;
  float smallSize = 14.0f * scale;
  float monoSize = 15.0f * scale;

  // Try to load regular font
  for (const char *fontPath : fontPaths) {
    if (std::filesystem::exists(fontPath)) {
      try {
        g_Fonts.regular =
            io.Fonts->AddFontFromFileTTF(fontPath, regularSize, &fontConfig);
        if (g_Fonts.regular) {
          fontLoaded = true;

          break;
        }
      } catch (const std::exception &e) {
        std::cerr << "Failed to load font " << fontPath << ": " << e.what()
                  << std::endl;
      }
    }
  }

  // Try to load bold font
  for (const char *fontPath : boldFontPaths) {
    if (std::filesystem::exists(fontPath)) {
      try {
        g_Fonts.bold =
            io.Fonts->AddFontFromFileTTF(fontPath, boldSize, &fontConfig);
        if (g_Fonts.bold) {

          break;
        }
      } catch (const std::exception &e) {
        std::cerr << "Failed to load bold font " << fontPath << ": " << e.what()
                  << std::endl;
      }
    }
  }

  // Load header font (use bold if available, otherwise regular)
  if (g_Fonts.bold) {
    g_Fonts.header =
        io.Fonts->AddFontFromFileTTF(boldFontPaths[0], headerSize, &fontConfig);
  } else if (g_Fonts.regular) {
    ImFontConfig headerConfig = fontConfig;
    headerConfig.SizePixels = headerSize;
    headerConfig.FontDataOwnedByAtlas =
        false; // CRITICAL: Do not free this memory as it is shared
    g_Fonts.header = io.Fonts->AddFontFromMemoryTTF(
        (void *)g_Fonts.regular->ConfigData->FontData,
        g_Fonts.regular->ConfigData->FontDataSize, headerSize, &headerConfig);
  }

  // Load small font
  if (fontLoaded && g_Fonts.regular) {
    ImFontConfig smallConfig = fontConfig;
    smallConfig.SizePixels = smallSize;
    smallConfig.FontDataOwnedByAtlas =
        false; // CRITICAL: Do not free this memory as it is shared
    g_Fonts.smallFont = io.Fonts->AddFontFromMemoryTTF(
        (void *)g_Fonts.regular->ConfigData->FontData,
        g_Fonts.regular->ConfigData->FontDataSize, smallSize, &smallConfig);
  }

  // Try to load monospace font
  for (const char *fontPath : monoFontPaths) {
    if (std::filesystem::exists(fontPath)) {
      try {
        g_Fonts.mono =
            io.Fonts->AddFontFromFileTTF(fontPath, monoSize, &fontConfig);
        if (g_Fonts.mono) {
          break;
        }
      } catch (const std::exception &e) {
        std::cerr << "Failed to load mono font " << fontPath << ": " << e.what()
                  << std::endl;
      }
    }
  }

  // Load FontAwesome icons - SOLUTION 1: Try merging with proper size scaling
  if (fontLoaded && g_Fonts.regular) {
    float baseFontSize = 16.0f * scale;
    float iconFontSize =
        baseFontSize * 2.0f /
        3.0f; // FA fonts need size reduction per Reddit solution

    // Create icon ranges
    static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};

    ImFontConfig *iconConfig = new ImFontConfig();
    iconConfig->MergeMode = true;
    iconConfig->PixelSnapH = true;
    iconConfig->GlyphMinAdvanceX = iconFontSize;
    iconConfig->GlyphRanges = icon_ranges;
    // Merge the icon glyphs into the regular (default) font. Without DstFont,
    // MergeMode targets the last-added font (mono), leaving the default font
    // without icon glyphs -- so ICON_FA_* embedded directly in Button()/Text()
    // labels rendered as '?' (e.g. the Snapshots tool's Capture/Restore/Edit/
    // Delete buttons). Merging into regular makes inline icons render anywhere.
    iconConfig->DstFont = g_Fonts.regular;

    // Try multiple paths for FontAwesome solid font
    const char *iconFontPaths[] = {"fonts/fa-solid-900.ttf",
                                   "StereoVista/fonts/fa-solid-900.ttf",
                                   "fa-solid-900.ttf"};

    bool iconsLoaded = false;
    for (const char *iconFontPath : iconFontPaths) {
      if (std::filesystem::exists(iconFontPath)) {
        // Load FontAwesome with correct size scaling
        g_Fonts.icons = io.Fonts->AddFontFromFileTTF(iconFontPath, iconFontSize,
                                                     iconConfig, icon_ranges);
        if (g_Fonts.icons) {
          iconsLoaded = true;
          break;
        }
      }
    }

    delete iconConfig; // Clean up

    if (!iconsLoaded) {
      std::cerr
          << "WARNING: Could not load FontAwesome icons with merge approach!"
          << std::endl;

      // SOLUTION 2: Try loading as separate font (no merge)
      for (const char *iconFontPath : iconFontPaths) {
        if (std::filesystem::exists(iconFontPath)) {
          g_Fonts.icons = io.Fonts->AddFontFromFileTTF(
              iconFontPath, iconFontSize, nullptr, icon_ranges);
          if (g_Fonts.icons) {
            std::cout << "FontAwesome loaded as SEPARATE font: " << iconFontPath
                      << std::endl;
            iconsLoaded = true;
            break;
          }
        }
      }
    }

    if (!iconsLoaded) {
      std::cerr << "CRITICAL: Could not load FontAwesome icons with any method!"
                << std::endl;
    }
  }

  // Also add Unicode emoji support for direct Unicode characters like 💡
  static const ImWchar emoji_ranges[] = {
      0x1F000, 0x1F6FF, // Miscellaneous Symbols and Pictographs
      0x1F300, 0x1F5FF, // Miscellaneous Symbols
      0x1F600, 0x1F64F, // Emoticons
      0x1F680, 0x1F6FF, // Transport and Map
      0x2600,  0x26FF,  // Miscellaneous Symbols
      0x2700,  0x27BF,  // Dingbats
      0xFE00,  0xFE0F,  // Variation Selectors
      0};

  ImFontConfig emoji_config;
  emoji_config.MergeMode = true;
  emoji_config.PixelSnapH = true;
  emoji_config.FontDataOwnedByAtlas = false;

  // Try to load system emoji fonts (Windows)
  const char *emojiFontPaths[] = {
      "C:\\Windows\\Fonts\\seguiemj.ttf", // Windows 10/11 Emoji font
      "C:\\Windows\\Fonts\\NotoColorEmoji.ttf",
      "C:\\Windows\\Fonts\\AppleColorEmoji.ttf"};

  for (const char *emojiFontPath : emojiFontPaths) {
    if (std::filesystem::exists(emojiFontPath)) {
      ImFont *emojiFont = io.Fonts->AddFontFromFileTTF(
          emojiFontPath, 16.0f * scale, &emoji_config, emoji_ranges);
      if (emojiFont) {
        break;
      }
    }
  }

  // Fallback to default font if nothing loaded
  if (!fontLoaded) {
    std::cout << "Using ImGui default font" << std::endl;
    g_Fonts.regular = io.Fonts->AddFontDefault();
    g_Fonts.bold = g_Fonts.regular;
    g_Fonts.header = g_Fonts.regular;
    g_Fonts.smallFont = g_Fonts.regular;
    g_Fonts.mono = g_Fonts.regular;
  }

  // Build font atlas
  io.Fonts->Build();

  // Set the default font to the regular font with merged icons
  if (g_Fonts.regular) {
    io.FontDefault = g_Fonts.regular;

    // Test if the regular font has the lightbulb after merging
    const ImFontGlyph *test_glyph = g_Fonts.regular->FindGlyph(0xf0eb);

  } else {
    std::cout << "WARNING: Regular font is null!" << std::endl;
  }

  return true;
}

float CalculateGuiScale(int windowWidth, int windowHeight) {
  float widthRatio = static_cast<float>(windowWidth) / 1920.0f;
  float heightRatio = static_cast<float>(windowHeight) / 1080.0f;
  float scale = std::min(widthRatio, heightRatio);
  return std::max(0.5f, std::min(2.0f, scale));
}

void UpdateGuiScale(int windowWidth, int windowHeight) {
  if (windowWidth <= 0 || windowHeight <= 0)
    return;

  float baseScale = CalculateGuiScale(windowWidth, windowHeight);
  float newScale = std::max(0.5f, std::min(2.0f, baseScale * g_GuiScale.userScaleFactor));
  bool isFirstCall =
      (g_GuiScale.lastWindowWidth == 0 && g_GuiScale.lastWindowHeight == 0);

  // Check if scale changed significantly (>5% threshold to avoid
  // micro-adjustments)
  float scaleDifference = std::abs(newScale - g_GuiScale.lastFontScale);
  bool scaleChangedSignificantly = (scaleDifference > 0.05f);

  if (isFirstCall || abs(windowWidth - g_GuiScale.lastWindowWidth) > 50 ||
      abs(windowHeight - g_GuiScale.lastWindowHeight) > 50) {

    if (isFirstCall || fabs(newScale - g_GuiScale.currentScale) > 0.05f) {
      g_GuiScale.currentScale = newScale;
      g_GuiScale.needsRescale = true;

      // Trigger font rebuild if scale changed significantly
      if (isFirstCall || scaleChangedSignificantly) {
        g_GuiScale.needsFontRebuild = true;
      }
    }

    g_GuiScale.lastWindowWidth = windowWidth;
    g_GuiScale.lastWindowHeight = windowHeight;
  }
}

void RescaleImGuiFonts(GLFWwindow *window, bool isDarkTheme) {
  if (!window || !ImGui::GetCurrentContext()) {
    g_GuiScale.needsRescale = false;
    g_GuiScale.needsFontRebuild = false;
    return;
  }

  // Rebuild font atlas if scale changed significantly
  if (g_GuiScale.needsFontRebuild) {
    RebuildImGuiFontAtlas(isDarkTheme);
    g_GuiScale.needsFontRebuild = false;
  }

  // Always update style to match current scale. Re-apply the active theme
  // (not the dark/light boolean) so a custom theme survives a window resize.
  if (g_GuiScale.needsRescale) {
    ApplyGuiTheme(g_currentTheme, 1.0f);
    g_GuiScale.needsRescale = false;
  }
}

void RebuildImGuiFontAtlas(bool isDarkTheme) {
  if (!ImGui::GetCurrentContext())
    return;

  ImGuiIO &io = ImGui::GetIO();
  float scale = g_GuiScale.currentScale;

  std::cout << "Rebuilding font atlas at scale " << scale << "x" << std::endl;

  // CRITICAL: Destroy the OpenGL texture BEFORE clearing fonts
  // This prevents accessing freed memory during texture cleanup
  ImGui_ImplOpenGL3_DestroyFontsTexture();

  // Clear existing fonts
  io.Fonts->Clear();

  // Reset font pointers (will be reassigned during rebuild)
  g_Fonts.regular = nullptr;
  g_Fonts.bold = nullptr;
  g_Fonts.header = nullptr;
  g_Fonts.smallFont = nullptr;
  g_Fonts.mono = nullptr;
  g_Fonts.icons = nullptr;

  // Font configuration with multiple options
  ImFontConfig fontConfig;
  fontConfig.OversampleH = 3;
  fontConfig.OversampleV = 3;
  fontConfig.PixelSnapH = true;
  fontConfig.RasterizerMultiply = 1.2f;

  // Modern font paths to try (in order of preference)
  const char *fontPaths[] = {// Windows fonts
                             "C:\\Windows\\Fonts\\SegoeUI.ttf",
                             "C:\\Windows\\Fonts\\Roboto-Regular.ttf",
                             "C:\\Windows\\Fonts\\Calibri.ttf",
                             "C:\\Windows\\Fonts\\Arial.ttf",
                             // Common installed fonts
                             "C:\\Program Files\\Microsoft\\Fonts\\SegoeUI.ttf",
                             // Fallback
                             "C:\\Windows\\Fonts\\Tahoma.ttf"};

  const char *boldFontPaths[] = {
      "C:\\Windows\\Fonts\\SegoeUI-Bold.ttf",
      "C:\\Windows\\Fonts\\Roboto-Bold.ttf", "C:\\Windows\\Fonts\\Calibrib.ttf",
      "C:\\Windows\\Fonts\\Arialbd.ttf", "C:\\Windows\\Fonts\\Tahomabd.ttf"};

  const char *monoFontPaths[] = {"C:\\Windows\\Fonts\\CascadiaCode.ttf",
                                 "C:\\Windows\\Fonts\\Consolas.ttf",
                                 "C:\\Windows\\Fonts\\CourierNew.ttf",
                                 "C:\\Windows\\Fonts\\LucidaConsole.ttf"};

  bool fontLoaded = false;

  // Define SCALED font sizes
  float regularSize = 16.0f * scale;
  float boldSize = 16.0f * scale;
  float headerSize = 20.0f * scale;
  float smallSize = 14.0f * scale;
  float monoSize = 15.0f * scale;

  // Track which font file was successfully loaded for regular font
  const char *loadedRegularFontPath = nullptr;
  const char *loadedBoldFontPath = nullptr;

  // Try to load regular font
  for (const char *fontPath : fontPaths) {
    if (std::filesystem::exists(fontPath)) {
      try {
        g_Fonts.regular =
            io.Fonts->AddFontFromFileTTF(fontPath, regularSize, &fontConfig);
        if (g_Fonts.regular) {
          fontLoaded = true;
          loadedRegularFontPath = fontPath;
          break;
        }
      } catch (const std::exception &e) {
        std::cerr << "Failed to load font " << fontPath << ": " << e.what()
                  << std::endl;
      }
    }
  }

  // Try to load bold font
  for (const char *fontPath : boldFontPaths) {
    if (std::filesystem::exists(fontPath)) {
      try {
        g_Fonts.bold =
            io.Fonts->AddFontFromFileTTF(fontPath, boldSize, &fontConfig);
        if (g_Fonts.bold) {
          loadedBoldFontPath = fontPath;
          break;
        }
      } catch (const std::exception &e) {
        std::cerr << "Failed to load bold font " << fontPath << ": " << e.what()
                  << std::endl;
      }
    }
  }

  // Load header font (use bold if available, otherwise regular)
  if (loadedBoldFontPath) {
    g_Fonts.header = io.Fonts->AddFontFromFileTTF(loadedBoldFontPath,
                                                  headerSize, &fontConfig);
  } else if (loadedRegularFontPath) {
    g_Fonts.header = io.Fonts->AddFontFromFileTTF(loadedRegularFontPath,
                                                  headerSize, &fontConfig);
  }

  // Load small font
  if (loadedRegularFontPath) {
    g_Fonts.smallFont = io.Fonts->AddFontFromFileTTF(loadedRegularFontPath,
                                                 smallSize, &fontConfig);
  }

  // Try to load monospace font
  for (const char *fontPath : monoFontPaths) {
    if (std::filesystem::exists(fontPath)) {
      try {
        g_Fonts.mono =
            io.Fonts->AddFontFromFileTTF(fontPath, monoSize, &fontConfig);
        if (g_Fonts.mono) {
          break;
        }
      } catch (const std::exception &e) {
        std::cerr << "Failed to load mono font " << fontPath << ": " << e.what()
                  << std::endl;
      }
    }
  }

  // Load FontAwesome icons - SOLUTION 1: Try merging with proper size scaling
  if (fontLoaded && g_Fonts.regular) {
    float baseFontSize = 16.0f * scale;
    float iconFontSize =
        baseFontSize * 2.0f / 3.0f; // FA fonts need size reduction

    // Create icon ranges
    static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};

    ImFontConfig *iconConfig = new ImFontConfig();
    iconConfig->MergeMode = true;
    iconConfig->PixelSnapH = true;
    iconConfig->GlyphMinAdvanceX = iconFontSize;
    iconConfig->GlyphRanges = icon_ranges;
    // Merge the icon glyphs into the regular (default) font. Without DstFont,
    // MergeMode targets the last-added font (mono), leaving the default font
    // without icon glyphs -- so ICON_FA_* embedded directly in Button()/Text()
    // labels rendered as '?' (e.g. the Snapshots tool's Capture/Restore/Edit/
    // Delete buttons). Merging into regular makes inline icons render anywhere.
    iconConfig->DstFont = g_Fonts.regular;

    // Try multiple paths for FontAwesome solid font
    const char *iconFontPaths[] = {"fonts/fa-solid-900.ttf",
                                   "StereoVista/fonts/fa-solid-900.ttf",
                                   "fa-solid-900.ttf"};

    bool iconsLoaded = false;
    for (const char *iconFontPath : iconFontPaths) {
      if (std::filesystem::exists(iconFontPath)) {
        // Load FontAwesome with correct size scaling
        g_Fonts.icons = io.Fonts->AddFontFromFileTTF(iconFontPath, iconFontSize,
                                                     iconConfig, icon_ranges);
        if (g_Fonts.icons) {
          iconsLoaded = true;
          break;
        }
      }
    }

    delete iconConfig; // Clean up

    if (!iconsLoaded) {
      // SOLUTION 2: Try loading as separate font (no merge)
      for (const char *iconFontPath : iconFontPaths) {
        if (std::filesystem::exists(iconFontPath)) {
          g_Fonts.icons = io.Fonts->AddFontFromFileTTF(
              iconFontPath, iconFontSize, nullptr, icon_ranges);
          if (g_Fonts.icons) {
            iconsLoaded = true;
            break;
          }
        }
      }
    }
  }

  // Also add Unicode emoji support for direct Unicode characters
  static const ImWchar emoji_ranges[] = {
      0x1F000, 0x1F6FF, // Miscellaneous Symbols and Pictographs
      0x1F300, 0x1F5FF, // Miscellaneous Symbols
      0x1F600, 0x1F64F, // Emoticons
      0x1F680, 0x1F6FF, // Transport and Map
      0x2600,  0x26FF,  // Miscellaneous Symbols
      0x2700,  0x27BF,  // Dingbats
      0xFE00,  0xFE0F,  // Variation Selectors
      0};

  ImFontConfig emoji_config;
  emoji_config.MergeMode = true;
  emoji_config.PixelSnapH = true;
  emoji_config.FontDataOwnedByAtlas = false;

  // Try to load system emoji fonts (Windows)
  const char *emojiFontPaths[] = {
      "C:\\Windows\\Fonts\\seguiemj.ttf", // Windows 10/11 Emoji font
      "C:\\Windows\\Fonts\\NotoColorEmoji.ttf",
      "C:\\Windows\\Fonts\\AppleColorEmoji.ttf"};

  for (const char *emojiFontPath : emojiFontPaths) {
    if (std::filesystem::exists(emojiFontPath)) {
      ImFont *emojiFont = io.Fonts->AddFontFromFileTTF(
          emojiFontPath, 16.0f * scale, &emoji_config, emoji_ranges);
      if (emojiFont) {
        break;
      }
    }
  }

  // Fallback to default font if nothing loaded
  if (!fontLoaded) {
    g_Fonts.regular = io.Fonts->AddFontDefault();
    g_Fonts.bold = g_Fonts.regular;
    g_Fonts.header = g_Fonts.regular;
    g_Fonts.smallFont = g_Fonts.regular;
    g_Fonts.mono = g_Fonts.regular;
  }

  // Build the new font atlas
  if (!io.Fonts->Build()) {
    std::cerr << "ERROR: Failed to build font atlas!" << std::endl;
    return;
  }

  // Upload to GPU - create new texture (old one already destroyed at start)
  ImGui_ImplOpenGL3_CreateFontsTexture();

  // Set default font
  if (g_Fonts.regular) {
    io.FontDefault = g_Fonts.regular;
  }

  // Update the last font scale
  g_GuiScale.lastFontScale = scale;

  std::cout << "Font atlas rebuilt successfully at " << scale << "x scale"
            << std::endl;
}

// Shared geometry (spacing, rounding, sizes). Applied for every theme.
static void ApplyStyleGeometry(ImGuiStyle &style) {
  float scale = g_GuiScale.currentScale;

  // Modern style parameters with improved spacing
  style.Alpha = 1.0f;
  style.DisabledAlpha = 0.5f;
  style.WindowPadding = ImVec2(15 * scale, 12 * scale);
  style.WindowRounding = 10.0f * scale;
  style.WindowBorderSize = 1.0f * scale;
  style.WindowMinSize = ImVec2(32 * scale, 32 * scale);
  style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
  style.WindowMenuButtonPosition = ImGuiDir_Left;
  style.ChildRounding = 8.0f * scale;
  style.ChildBorderSize = 1.0f * scale;
  style.PopupRounding = 8.0f * scale;
  style.PopupBorderSize = 1.0f * scale;
  style.FramePadding = ImVec2(10 * scale, 7 * scale);
  style.FrameRounding = 7.0f * scale;
  style.FrameBorderSize = 0.0f;
  style.ItemSpacing = ImVec2(10 * scale, 9 * scale);
  style.ItemInnerSpacing = ImVec2(8 * scale, 6 * scale);
  style.CellPadding = ImVec2(6 * scale, 4 * scale);
  style.IndentSpacing = 22.0f * scale;
  style.ColumnsMinSpacing = 6.0f * scale;
  style.ScrollbarSize = 14.0f * scale;
  style.ScrollbarRounding = 12.0f * scale;
  style.GrabMinSize = 14.0f * scale;
  style.GrabRounding = 8.0f * scale;
  style.TabRounding = 8.0f * scale;
  style.TabBorderSize = 0.0f;
  style.TabMinWidthForCloseButton = 0.0f;
  style.ColorButtonPosition = ImGuiDir_Right;
  style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
  style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

  // Multi-viewport: a window dragged out of the main window becomes a real,
  // opaque OS window. Rounded window corners would leave that window's
  // framebuffer corners uncovered (rendering as black), so square off
  // top-level windows when viewports are enabled. This also matches the
  // docked-panel aesthetic. Inner widgets keep their rounding.
  if (ImGui::GetCurrentContext() &&
      (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
    style.WindowRounding = 0.0f;
  }
}

// Dark-theme color engine. Reads the four background levels and accents from
// the palette (lvl0 darkest .. lvl3 lightest) and lays out every ImGui color
// the same way the original dark theme did, so any dark palette gets a
// consistent, tuned result.
static void ApplyDarkColors(ImGuiStyle &style, const ThemePalette &p) {
    ImVec4 *colors = style.Colors;

    const ImVec4 bgDark = p.lvl0;
    const ImVec4 bgMedium = p.lvl1;
    const ImVec4 bgLight = p.lvl2;
    const ImVec4 bgVeryLight = p.lvl3;

    const ImVec4 textPrimary = p.textPrimary;
    const ImVec4 textDisabled = p.textDisabled;

    const ImVec4 accentPrimary = p.accent;
    const ImVec4 accentHover = p.accentHover;
    const ImVec4 accentActive = p.accentActive;

    // Background colors
    colors[ImGuiCol_WindowBg] = bgMedium;
    colors[ImGuiCol_ChildBg] = ImVec4(bgDark.x, bgDark.y, bgDark.z, 0.00f);
    colors[ImGuiCol_PopupBg] = bgLight;
    colors[ImGuiCol_Border] =
        ImVec4(bgVeryLight.x, bgVeryLight.y, bgVeryLight.z, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Text colors
    colors[ImGuiCol_Text] = textPrimary;
    colors[ImGuiCol_TextDisabled] = textDisabled;

    // Headers
    colors[ImGuiCol_Header] =
        ImVec4(bgVeryLight.x, bgVeryLight.y, bgVeryLight.z, 0.55f);
    colors[ImGuiCol_HeaderHovered] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.30f);
    colors[ImGuiCol_HeaderActive] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.50f);

    // Buttons
    colors[ImGuiCol_Button] =
        ImVec4(bgVeryLight.x, bgVeryLight.y, bgVeryLight.z, 0.60f);
    colors[ImGuiCol_ButtonHovered] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.30f);
    colors[ImGuiCol_ButtonActive] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.50f);

    // Frame backgrounds. Kept solid enough that input boxes, combos and
    // sliders read as distinct controls against the window background.
    colors[ImGuiCol_FrameBg] =
        ImVec4(bgVeryLight.x, bgVeryLight.y, bgVeryLight.z, 0.52f);
    colors[ImGuiCol_FrameBgHovered] =
        ImVec4(bgVeryLight.x, bgVeryLight.y, bgVeryLight.z, 0.68f);
    colors[ImGuiCol_FrameBgActive] =
        ImVec4(bgVeryLight.x, bgVeryLight.y, bgVeryLight.z, 0.80f);

    // Tabs
    colors[ImGuiCol_Tab] = bgLight;
    colors[ImGuiCol_TabHovered] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.30f);
    colors[ImGuiCol_TabActive] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.40f);
    colors[ImGuiCol_TabUnfocused] = bgLight;
    colors[ImGuiCol_TabUnfocusedActive] =
        ImVec4(bgVeryLight.x, bgVeryLight.y, bgVeryLight.z, 0.70f);

    // Title
    colors[ImGuiCol_TitleBg] = bgDark;
    colors[ImGuiCol_TitleBgActive] =
        ImVec4(bgDark.x, bgDark.y, bgDark.z, 0.90f);
    colors[ImGuiCol_TitleBgCollapsed] =
        ImVec4(bgDark.x, bgDark.y, bgDark.z, 0.75f);
    colors[ImGuiCol_MenuBarBg] = bgMedium;

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(bgDark.x, bgDark.y, bgDark.z, 0.60f);
    colors[ImGuiCol_ScrollbarGrab] =
        ImVec4(bgVeryLight.x, bgVeryLight.y, bgVeryLight.z, 0.30f);
    colors[ImGuiCol_ScrollbarGrabHovered] =
        ImVec4(bgVeryLight.x, bgVeryLight.y, bgVeryLight.z, 0.40f);
    colors[ImGuiCol_ScrollbarGrabActive] =
        ImVec4(bgVeryLight.x, bgVeryLight.y, bgVeryLight.z, 0.50f);

    // Checkbox & Radio
    colors[ImGuiCol_CheckMark] = accentPrimary;
    colors[ImGuiCol_SliderGrab] = accentPrimary;
    colors[ImGuiCol_SliderGrabActive] = accentActive;

    // Separator
    colors[ImGuiCol_Separator] =
        ImVec4(bgVeryLight.x, bgVeryLight.y, bgVeryLight.z, 0.50f);
    colors[ImGuiCol_SeparatorHovered] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.50f);
    colors[ImGuiCol_SeparatorActive] = accentPrimary;

    // Resize
    colors[ImGuiCol_ResizeGrip] =
        ImVec4(bgVeryLight.x, bgVeryLight.y, bgVeryLight.z, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.50f);
    colors[ImGuiCol_ResizeGripActive] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.70f);

    // Plot
    colors[ImGuiCol_PlotLines] = accentPrimary;
    colors[ImGuiCol_PlotLinesHovered] = accentHover;
    colors[ImGuiCol_PlotHistogram] = accentPrimary;
    colors[ImGuiCol_PlotHistogramHovered] = accentHover;

    // Table
    colors[ImGuiCol_TableHeaderBg] = bgVeryLight;
    colors[ImGuiCol_TableBorderStrong] =
        ImVec4(bgVeryLight.x, bgVeryLight.y, bgVeryLight.z, 0.60f);
    colors[ImGuiCol_TableBorderLight] =
        ImVec4(bgVeryLight.x, bgVeryLight.y, bgVeryLight.z, 0.30f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    // Misc
    colors[ImGuiCol_TextSelectedBg] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.35f);
    colors[ImGuiCol_DragDropTarget] = accentPrimary;
    colors[ImGuiCol_NavHighlight] = accentPrimary;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
}

// Light-theme color engine. Reads the four background levels from the palette
// (lvl0 lightest .. lvl3 darkest) and the accents, reproducing the original
// light theme's layout for any light palette.
static void ApplyLightColors(ImGuiStyle &style, const ThemePalette &p) {
    ImVec4 *colors = style.Colors;

    const ImVec4 bgWhite = p.lvl0;
    const ImVec4 bgLight = p.lvl1;
    const ImVec4 bgMedium = p.lvl2;
    const ImVec4 bgDark = p.lvl3;

    const ImVec4 textPrimary = p.textPrimary;
    const ImVec4 textDisabled = p.textDisabled;

    const ImVec4 accentPrimary = p.accent;
    const ImVec4 accentHover = p.accentHover;
    const ImVec4 accentActive = p.accentActive;

    // Background colors
    colors[ImGuiCol_WindowBg] = bgWhite;
    colors[ImGuiCol_ChildBg] = ImVec4(bgWhite.x, bgWhite.y, bgWhite.z, 0.00f);
    colors[ImGuiCol_PopupBg] = bgWhite;
    colors[ImGuiCol_Border] = ImVec4(bgMedium.x, bgMedium.y, bgMedium.z, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.05f);

    // Text
    colors[ImGuiCol_Text] = textPrimary;
    colors[ImGuiCol_TextDisabled] = textDisabled;

    // Headers
    colors[ImGuiCol_Header] = ImVec4(bgLight.x, bgLight.y, bgLight.z, 0.80f);
    colors[ImGuiCol_HeaderHovered] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.20f);
    colors[ImGuiCol_HeaderActive] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.30f);

    // Buttons
    colors[ImGuiCol_Button] = bgLight;
    colors[ImGuiCol_ButtonHovered] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.15f);
    colors[ImGuiCol_ButtonActive] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.25f);

    // Frame backgrounds
    colors[ImGuiCol_FrameBg] = bgLight;
    colors[ImGuiCol_FrameBgHovered] =
        ImVec4(bgMedium.x, bgMedium.y, bgMedium.z, 0.50f);
    colors[ImGuiCol_FrameBgActive] =
        ImVec4(bgMedium.x, bgMedium.y, bgMedium.z, 0.70f);

    // Tabs
    colors[ImGuiCol_Tab] = bgLight;
    colors[ImGuiCol_TabHovered] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.20f);
    colors[ImGuiCol_TabActive] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.30f);
    colors[ImGuiCol_TabUnfocused] = bgLight;
    colors[ImGuiCol_TabUnfocusedActive] = bgMedium;

    // Title
    colors[ImGuiCol_TitleBg] = bgLight;
    colors[ImGuiCol_TitleBgActive] = bgMedium;
    colors[ImGuiCol_TitleBgCollapsed] = bgLight;
    colors[ImGuiCol_MenuBarBg] = bgWhite;

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = bgLight;
    colors[ImGuiCol_ScrollbarGrab] = bgMedium;
    colors[ImGuiCol_ScrollbarGrabHovered] = bgDark;
    colors[ImGuiCol_ScrollbarGrabActive] = accentPrimary;

    // Check/Radio/Slider
    colors[ImGuiCol_CheckMark] = accentPrimary;
    colors[ImGuiCol_SliderGrab] = accentPrimary;
    colors[ImGuiCol_SliderGrabActive] = accentActive;

    // Separator
    colors[ImGuiCol_Separator] = bgMedium;
    colors[ImGuiCol_SeparatorHovered] = accentPrimary;
    colors[ImGuiCol_SeparatorActive] = accentActive;

    // Resize
    colors[ImGuiCol_ResizeGrip] =
        ImVec4(bgMedium.x, bgMedium.y, bgMedium.z, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.50f);
    colors[ImGuiCol_ResizeGripActive] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.70f);

    // Plot
    colors[ImGuiCol_PlotLines] = accentPrimary;
    colors[ImGuiCol_PlotLinesHovered] = accentHover;
    colors[ImGuiCol_PlotHistogram] = accentPrimary;
    colors[ImGuiCol_PlotHistogramHovered] = accentHover;

    // Table
    colors[ImGuiCol_TableHeaderBg] = bgMedium;
    colors[ImGuiCol_TableBorderStrong] = bgDark;
    colors[ImGuiCol_TableBorderLight] = bgMedium;
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.00f, 0.00f, 0.00f, 0.03f);

    // Misc
    colors[ImGuiCol_TextSelectedBg] =
        ImVec4(accentPrimary.x, accentPrimary.y, accentPrimary.z, 0.25f);
    colors[ImGuiCol_DragDropTarget] = accentPrimary;
    colors[ImGuiCol_NavHighlight] = accentPrimary;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.70f, 0.70f, 0.70f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);
}

// Apply a full color theme: shared geometry, the palette-derived colors via the
// matching (dark/light) engine, and the published g_StyleColors used across the
// GUI. Records the theme so GUI-scale restyles keep the user's choice.
void ApplyGuiTheme(int theme, float alpha) {
  if (theme < 0 || theme >= GUI_THEME_COUNT)
    theme = GUI_THEME_MODERN_DARK;
  g_currentTheme = theme;

  const ThemePalette &p = GetPalette(theme);
  ImGuiStyle &style = ImGui::GetStyle();

  ApplyStyleGeometry(style);

  // Publish accent/status colors consumed throughout the GUI (section headers,
  // toasts, the performance overlay, etc.).
  g_StyleColors.primary = p.accent;
  g_StyleColors.primaryHover = p.accentHover;
  g_StyleColors.primaryActive = p.accentActive;
  g_StyleColors.secondary = p.lvl3;
  g_StyleColors.accent = p.accent;
  g_StyleColors.success = p.success;
  g_StyleColors.warning = p.warning;
  g_StyleColors.danger = p.danger;
  g_StyleColors.info = p.info;

  if (p.isDark)
    ApplyDarkColors(style, p);
  else
    ApplyLightColors(style, p);

  // Apply alpha if needed
  if (alpha < 1.0f) {
    ImVec4 *colors = style.Colors;
    for (int i = 0; i < ImGuiCol_COUNT; i++) {
      colors[i].w *= alpha;
    }
  }
}

// Backward-compatible entry point: maps the old dark/light boolean onto the two
// built-in themes.
void SetupImGuiStyle(bool bStyleDark_, float alpha_) {
  ApplyGuiTheme(bStyleDark_ ? GUI_THEME_MODERN_DARK : GUI_THEME_MODERN_LIGHT,
                alpha_);
}

// Custom widget styling functions
void PushSectionHeaderStyle() {
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 12));
}

void PopSectionHeaderStyle() {
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

void PushCompactStyle() {
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
}

void PopCompactStyle() { ImGui::PopStyleVar(2); }

void ApplyCustomWidgetStyles() {
  ImGuiStyle &style = ImGui::GetStyle();

  // Custom button colors for different button types
  // These can be used with PushStyleColor before buttons
}

void ApplyStylePreset(GuiStylePreset preset) {
  switch (preset) {
  case GuiStylePreset::MODERN_DARK:
    SetupImGuiStyle(true, 1.0f);
    break;
  case GuiStylePreset::MODERN_LIGHT:
    SetupImGuiStyle(false, 1.0f);
    break;
  case GuiStylePreset::HIGH_CONTRAST:
    SetupImGuiStyle(true, 1.0f);
    // Increase contrast
    ImGui::GetStyle().Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] =
        ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    break;
  case GuiStylePreset::MINIMAL:
    SetupImGuiStyle(false, 1.0f);
    // Reduce visual clutter
    ImGui::GetStyle().WindowRounding = 0.0f;
    ImGui::GetStyle().FrameRounding = 0.0f;
    ImGui::GetStyle().ScrollbarRounding = 0.0f;
    ImGui::GetStyle().GrabRounding = 0.0f;
    break;
  }
}