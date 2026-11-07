// dkr360: the console's own menu, after the Star Fox 64 port's (sf360_menu.cpp). Back opens and closes it; the D-pad or
// the left stick moves, A changes or confirms, B goes back, LB/RB change the tab. It reads the pads from XAM itself and
// keeps them from the game while it is open (platform_sdl_min.c asks dkr360_menu_wants_input). Its settings are Golden
// Balloon's own (dkr360\video.ini, through mdkr_video_config_runtime_set) plus the FPS counter (dkr360\console.ini).
// Everything runs on the game's thread, which also draws (Golden Balloon renders where it simulates): the pad and the
// changes when a frame is ending (gfx_xenos.cpp's end_frame calls dkr360_menu_render), the drawing right after, with
// Dear ImGui's draw lists into Xenos.

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

extern "C" {
#include <xsdk/kernel.h>
#include <xsdk/log.h>

#include "video_config.h"
}

#include "dkr360_font_roboto.h"

extern "C" {
uint32_t dkr360_ui_texture(const uint8_t* rgba, int width, int height);
void dkr360_ui_draw(uint32_t textureId, const void* vertices, uint32_t vertexCount, const uint16_t* indices,
                    uint32_t indexCount, int clipX, int clipY, int clipW, int clipH);
float dkr360_video_fps(void);
void platform_request_exit(int code);
int SDL_NumJoysticks(void);
}

namespace {

// ------------------------------------------------------------------------------------------------ settings

// Golden Balloon's setting as the menu shows it, and changing it (saved by Golden Balloon in dkr360\video.ini).
const MdkrVideoValue& Value(MdkrVideoKey key) {
    return mdkr_video_config_current()->values[key];
}

bool Set(MdkrVideoKey key, const char* value) {
    const MdkrVideoRuntimeResult r = mdkr_video_config_runtime_set(key, value);
    if (!mdkr_video_runtime_result_applied(r)) {
        xsdk_log_write("dkr360 menu: %s=%s not applied (%d)", mdkr_video_schema(key)->name, value, (int)r);
        return false;
    }
    return true;
}

bool SetInt(MdkrVideoKey key, int v) {
    char buf[16];
    snprintf(buf, sizeof buf, "%d", v);
    return Set(key, buf);
}

// The menu's own: the FPS counter and the language, in dkr360\console.ini (dkr360_main.c reads the language too,
// to pick the game's ROM).
bool sShowFps = false;
int sLanguageSetting = 0; // 0 automatic (the console's), 1 English, 2 Portuguese (Brazil), 3 Spanish

void SaveConsoleSettings() {
    if (FILE* f = fopen("dkr360/console.ini", "w")) {
        fprintf(f, "ShowFps=%d\nLanguage=%d\n", sShowFps ? 1 : 0, sLanguageSetting);
        fclose(f);
    }
}

// The language the menu shows: 0 English, 1 Portuguese, 2 Spanish (dkr360_main.c, the same rule for the ROM).
extern "C" int dkr360_language_resolve(int setting);
int Lang() {
    return dkr360_language_resolve(sLanguageSetting);
}

// One text in the three languages, picked when the menu is built (Tabs) or drawn.
const char* L(const char* en, const char* pt, const char* es) {
    switch (Lang()) {
        case 1:
            return pt;
        case 2:
            return es;
        default:
            return en;
    }
}

void LoadConsoleSettings() {
    if (FILE* f = fopen("dkr360/console.ini", "r")) {
        char line[128];
        while (fgets(line, sizeof line, f)) {
            int v;
            if (sscanf(line, "ShowFps=%d", &v) == 1) {
                sShowFps = v != 0;
            } else if (sscanf(line, "Language=%d", &v) == 1 && v >= 0 && v <= 3) {
                sLanguageSetting = v;
            }
        }
        fclose(f);
    }
}

// ------------------------------------------------------------------------------------------------ items

enum Kind { K_CHOICE, K_PERCENT, K_ACTION, K_INFO };

struct Choice {
    const char* value; // what goes into the setting
    const char* label;
};

struct Item {
    Kind kind;
    const char* label;
    const char* desc;
    // K_CHOICE: the current choice's index, and setting one
    std::function<int()> get;
    std::function<void(int)> set;
    std::vector<Choice> choices;
    // K_PERCENT
    MdkrVideoKey key;
    // K_ACTION
    std::function<void()> run;
    bool confirm;
    // K_INFO
    const char* info;
    // Restore defaults: the choice index or percentage to go back to (-1: none)
    int def;
    int id; // ID_*: items the menu treats apart
};

enum { ID_NONE, ID_DEFAULTS, ID_LANGUAGE, ID_RESTART };

struct Tab {
    const char* label;
    std::vector<Item> items;
};

int IndexOfText(MdkrVideoKey key, const std::vector<Choice>& choices) {
    const char* now = Value(key).text;
    for (size_t i = 0; i < choices.size(); i++) {
        if (strcasecmp(now, choices[i].value) == 0) {
            return (int)i;
        }
    }
    return 0;
}

int IndexOfNumber(MdkrVideoKey key, const std::vector<Choice>& choices) {
    const int now = (int)lroundf(Value(key).number);
    for (size_t i = 0; i < choices.size(); i++) {
        if (atoi(choices[i].value) == now) {
            return (int)i;
        }
    }
    return 0;
}

// A setting held as text (Video.Aspect "auto", Camera.Obstruction "modern"...).
Item TextChoice(const char* label, const char* desc, MdkrVideoKey key, std::vector<Choice> choices, int def = 0) {
    Item i{};
    i.kind = K_CHOICE;
    i.label = label;
    i.desc = desc;
    i.choices = choices;
    i.get = [key, choices] { return IndexOfText(key, choices); };
    i.set = [key, choices](int idx) { Set(key, choices[idx].value); };
    i.def = def;
    return i;
}

// A setting held as a number (Video.WidescreenHUD 1, Enhancements.DrawDistance 200...).
Item NumberChoice(const char* label, const char* desc, MdkrVideoKey key, std::vector<Choice> choices, int def = 0) {
    Item i{};
    i.kind = K_CHOICE;
    i.label = label;
    i.desc = desc;
    i.choices = choices;
    i.get = [key, choices] { return IndexOfNumber(key, choices); };
    i.set = [key, choices](int idx) { SetInt(key, atoi(choices[idx].value)); };
    i.def = def;
    return i;
}

Item Percent(const char* label, const char* desc, MdkrVideoKey key, int def) {
    Item i{};
    i.kind = K_PERCENT;
    i.label = label;
    i.desc = desc;
    i.key = key;
    i.def = def;
    return i;
}

Item Action(const char* label, const char* desc, std::function<void()> run, bool confirm) {
    Item i{};
    i.kind = K_ACTION;
    i.label = label;
    i.desc = desc;
    i.run = std::move(run);
    i.confirm = confirm;
    i.def = -1;
    return i;
}

Item Info(const char* label, const char* info, const char* desc) {
    Item i{};
    i.kind = K_INFO;
    i.label = label;
    i.desc = desc;
    i.info = info;
    i.def = -1;
    return i;
}

// Frame rate: two of Golden Balloon's settings together, as its own "Smooth" quick choice does.
int GetFrameRate() {
    return strcasecmp(Value(MDKR_VIDEO_MOTION_SMOOTHING).text, "interpolate") == 0 ? 0 : 1;
}

void SetFrameRate(int idx) {
    const MdkrVideoRuntimeChange smooth[2] = { { MDKR_VIDEO_FRAME_LIMIT, "60" },
                                               { MDKR_VIDEO_MOTION_SMOOTHING, "interpolate" } };
    const MdkrVideoRuntimeChange original[2] = { { MDKR_VIDEO_FRAME_LIMIT, "original" },
                                                 { MDKR_VIDEO_MOTION_SMOOTHING, "off" } };
    const MdkrVideoRuntimeResult r = mdkr_video_config_runtime_set_many(idx == 0 ? smooth : original, 2);
    if (!mdkr_video_runtime_result_applied(r)) {
        xsdk_log_write("dkr360 menu: frame rate %d not applied (%d)", idx, (int)r);
    }
}

std::vector<Tab>& Tabs();

void RestoreDefaults() {
    for (Tab& tab : Tabs()) {
        for (Item& item : tab.items) {
            if (item.def < 0) {
                continue;
            }
            if (item.kind == K_CHOICE && item.set) {
                item.set(item.def);
            } else if (item.kind == K_PERCENT) {
                SetInt(item.key, item.def);
            }
        }
    }
}

std::vector<Tab> sTabs;
bool sRebuildTabs = false;

// The game's language: the ROM dkr360_main.c loaded for it, and whether the chosen one is a restart away.
extern "C" int dkr360_rom_language(void);           // 0 English (the cartridge's), 1 Portuguese, 2 Spanish
extern "C" int dkr360_rom_available(int language);   // its translated ROM is beside the .xex
extern "C" void dkr360_restart(void);

std::vector<Tab>& Tabs() {
    if (!sTabs.empty()) {
        return sTabs;
    }
    const char* on = L("On", "Ligado", "Activado");
    const char* off = L("Off", "Desligado", "Desactivado");
    const char* original = L("Original", "Original", "Original");

    Item lang{};
    lang.kind = K_CHOICE;
    lang.id = ID_LANGUAGE;
    lang.label = L("Language", "Idioma", "Idioma");
    lang.desc = L("This menu's language and the game's. The game's texts in Portuguese or Spanish come from its "
                  "translated ROM (dkr_pt.z64 or dkr_es.z64 beside the .xex); switching to them restarts the game.",
                  "O idioma deste menu e do jogo. Os textos do jogo em português ou espanhol vêm da ROM traduzida "
                  "(dkr_pt.z64 ou dkr_es.z64 ao lado do .xex); trocar para eles reinicia o jogo.",
                  "El idioma de este menú y del juego. Los textos del juego en portugués o español vienen de su ROM "
                  "traducida (dkr_pt.z64 o dkr_es.z64 junto al .xex); cambiar a ellos reinicia el juego.");
    lang.choices = { { "0", L("Automatic", "Automático", "Automático") }, { "1", "English" },
                     { "2", "Português (Brasil)" }, { "3", "Español" } };
    lang.get = [] { return sLanguageSetting; };
    lang.set = [](int idx) {
        sLanguageSetting = idx;
        SaveConsoleSettings();
        sRebuildTabs = true; // rebuilt in the new language before the next frame (not now: this runs inside a tab)
    };
    lang.def = -1;

    Item gameText = Info(L("Game texts", "Textos do jogo", "Textos del juego"), "",
                         L("The language the game's own texts are in now. Missing a translated ROM, the game "
                           "stays in English.",
                           "O idioma em que os textos do próprio jogo estão agora. Sem a ROM traduzida, o jogo "
                           "fica em inglês.",
                           "El idioma en que están ahora los textos del propio juego. Sin la ROM traducida, el "
                           "juego queda en inglés."));
    gameText.id = ID_RESTART;

    Item fps{};
    fps.kind = K_CHOICE;
    fps.label = L("Show FPS counter", "Mostrar contador de FPS", "Mostrar contador de FPS");
    fps.desc = L("A small frames-per-second counter in the top right corner of the screen.",
                 "Um pequeno contador de quadros por segundo no canto superior direito da tela.",
                 "Un pequeño contador de fotogramas por segundo en la esquina superior derecha de la pantalla.");
    fps.choices = { { "0", off }, { "1", on } };
    fps.get = [] { return sShowFps ? 1 : 0; };
    fps.set = [](int idx) {
        sShowFps = idx == 1;
        SaveConsoleSettings();
    };
    fps.def = 0;

    Item defaults = Action(L("Restore defaults", "Restaurar padrões", "Restaurar valores"),
                           L("Puts every option of this menu back to its starting value.",
                             "Volta todas as opções deste menu ao valor inicial.",
                             "Devuelve todas las opciones de este menú a su valor inicial."),
                           [] { RestoreDefaults(); }, true);
    defaults.id = ID_DEFAULTS;

    sTabs.push_back({ L("General", "Geral", "General"),
                      {
                          lang,
                          gameText,
                          fps,
                          NumberChoice(L("Rumble", "Vibração", "Vibración"),
                                       L("Controller vibration when racers hit, boost or get hit.",
                                         "O controle vibra em batidas, turbos e quando você é atingido.",
                                         "El mando vibra en choques, turbos y cuando te golpean."),
                                       MDKR_INPUT_RUMBLE_ENABLED, { { "1", on }, { "0", off } }),
                          defaults,
                          Action(L("Quit to dashboard", "Sair para o dashboard", "Salir al panel"),
                                 L("Leaves the game. Progress the game hasn't saved yet is lost.",
                                   "Sai do jogo. O progresso que o jogo ainda não salvou é perdido.",
                                   "Sale del juego. El progreso que el juego aún no guardó se pierde."),
                                 [] { platform_request_exit(0); }, true),
                      } });
    sTabs.push_back(
        { L("Video", "Vídeo", "Vídeo"),
          {
              TextChoice(L("Aspect ratio", "Proporção da tela", "Relación de aspecto"),
                         L("16:9 fills the screen and shows more of the track at the sides. 4:3 is the original "
                           "picture, with black bars.",
                           "16:9 ocupa a tela toda e mostra mais da pista nas laterais. 4:3 é a imagem original, com "
                           "barras pretas.",
                           "16:9 llena la pantalla y muestra más de la pista a los lados. 4:3 es la imagen original, "
                           "con barras negras."),
                         MDKR_VIDEO_ASPECT,
                         { { "auto", L("16:9 (widescreen)", "16:9 (widescreen)", "16:9 (panorámica)") },
                           { "4:3", "4:3 (original)" } }),
              Item{ K_CHOICE, L("Frame rate", "Taxa de quadros", "Fotogramas por segundo"),
                    L("60 smooths the motion with in-between frames; the race itself keeps its original speed. 30 is "
                      "the game's original frame rate.",
                      "60 suaviza o movimento com quadros intermediários; a corrida mantém a velocidade original. 30 "
                      "é a taxa original do jogo.",
                      "60 suaviza el movimiento con fotogramas intermedios; la carrera mantiene su velocidad original. "
                      "30 es la tasa original del juego."),
                    GetFrameRate, SetFrameRate,
                    { { "60", L("60 (smooth)", "60 (suave)", "60 (suave)") }, { "30", "30 (original)" } } },
              NumberChoice(L("HUD placement", "Posição do HUD", "Posición del HUD"),
                           L("At the edges of a widescreen picture, or in the centre 4:3 frame as on the N64.",
                             "Nas bordas da imagem widescreen, ou no quadro 4:3 central como no N64.",
                             "En los bordes de la imagen panorámica, o en el cuadro 4:3 central como en la N64."),
                           MDKR_VIDEO_WIDESCREEN_HUD,
                           { { "1", L("Screen edges", "Bordas da tela", "Bordes de la pantalla") },
                             { "0", L("Centre 4:3", "Centro 4:3", "Centro 4:3") } },
                           0),
              TextChoice(L("Field of view", "Campo de visão", "Campo de visión"),
                         L("How wide the race camera sees. Original keeps every track's own lens.",
                           "Quanto a câmera da corrida enxerga. Original mantém a lente de cada pista.",
                           "Cuánto ve la cámara de la carrera. Original mantiene la lente de cada pista."),
                         MDKR_VIDEO_GAMEPLAY_FOV,
                         { { "authored", original }, { "70", "70" }, { "80", "80" }, { "90", "90" } }),
              NumberChoice(L("Draw distance", "Distância de desenho", "Distancia de dibujado"),
                           L("How far ahead scenery, bananas and balloons appear. Farther costs some speed.",
                             "A que distância cenário, bananas e balões aparecem. Mais longe custa desempenho.",
                             "A qué distancia aparecen escenario, plátanos y globos. Más lejos cuesta rendimiento."),
                           MDKR_ENH_DRAW_DISTANCE,
                           { { "100", original }, { "150", "150%" }, { "200", "200%" }, { "400", "400%" } }),
              NumberChoice(L("Speedometer", "Velocímetro", "Velocímetro"),
                           L("Shows your speed during races.", "Mostra sua velocidade nas corridas.",
                             "Muestra tu velocidad en las carreras."),
                           MDKR_ENH_SPEEDOMETER, { { "0", off }, { "1", on } }),
          } });
    sTabs.back().items[1].def = 0;
    sTabs.push_back({ L("Audio", "Áudio", "Audio"),
                      {
                          Percent(L("Master volume", "Volume geral", "Volumen general"),
                                  L("The volume of the whole game.", "O volume do jogo todo.",
                                    "El volumen de todo el juego."),
                                  MDKR_AUDIO_MASTER_VOLUME, 85),
                          Percent(L("Music", "Música", "Música"),
                                  L("The volume of the music.", "O volume das músicas.", "El volumen de la música."),
                                  MDKR_AUDIO_MUSIC_VOLUME, 100),
                          Percent(L("Sound effects", "Efeitos sonoros", "Efectos de sonido"),
                                  L("Engines, items, voices and everything else.",
                                    "Motores, itens, vozes e todo o resto.",
                                    "Motores, objetos, voces y todo lo demás."),
                                  MDKR_AUDIO_EFFECTS_VOLUME, 100),
                      } });
    sTabs.push_back(
        { L("Game", "Jogo", "Juego"),
          {
              TextChoice(L("Opponent skill", "Dificuldade dos rivais", "Dificultad de los rivales"),
                         L("Original keeps the computer racers' own speed. Hard and Brutal make them faster. Takes "
                           "effect after restarting the game.",
                           "Original mantém a velocidade dos rivais do computador. Difícil e Brutal os deixam mais "
                           "rápidos. Vale depois de reiniciar o jogo.",
                           "Original mantiene la velocidad de los rivales de la consola. Difícil y Brutal los hacen "
                           "más rápidos. Se aplica al reiniciar el juego."),
                         MDKR_ENH_AI_DIFFICULTY,
                         { { "original", original }, { "hard", L("Hard", "Difícil", "Difícil") },
                           { "brutal", L("Brutal", "Brutal", "Brutal") } }),
              TextChoice(L("Camera", "Câmera", "Cámara"),
                         L("Original is the game's own camera. Avoid walls keeps it in front of walls and doors that "
                           "would come between it and your racer. Takes effect at the next race.",
                           "Original é a câmera do jogo. Desviar de paredes a mantém na frente de paredes e portas "
                           "que ficariam entre ela e o seu corredor. Vale a partir da próxima corrida.",
                           "Original es la cámara del juego. Evitar paredes la mantiene delante de paredes y puertas "
                           "que quedarían entre ella y tu corredor. Se aplica desde la próxima carrera."),
                         MDKR_VIDEO_CAMERA_OBSTRUCTION,
                         { { "observe", original },
                           { "modern", L("Avoid walls", "Desviar de paredes", "Evitar paredes") } }),
              NumberChoice(L("Adventure Party", "Aventura em grupo", "Aventura en grupo"),
                           L("Lets two to four players explore Adventure mode together in split screen.",
                             "Deixa dois a quatro jogadores explorarem o modo Aventura juntos em tela dividida.",
                             "Permite que de dos a cuatro jugadores exploren el modo Aventura juntos en pantalla "
                             "dividida."),
                           MDKR_ENH_ADVENTURE_PARTY, { { "0", off }, { "1", on } }),
              NumberChoice(L("Bonus racers", "Corredores bônus", "Corredores extra"),
                           L("Adds the bonus racers to the character select. Takes effect after restarting the game.",
                             "Adiciona os corredores bônus à escolha de personagem. Vale depois de reiniciar o jogo.",
                             "Añade los corredores extra a la selección de personaje. Se aplica al reiniciar el "
                             "juego."),
                           MDKR_CONTENT_BONUS_RACERS, { { "0", off }, { "1", on } }),
          } });
    sTabs.push_back(
        { L("Credits", "Créditos", "Créditos"),
          {
              Info(L("Xbox 360 port", "Port para Xbox 360", "Port para Xbox 360"), "Matherzin",
                   L("Diddy Kong Racing on the Xbox 360, ported by Matherzin.",
                     "Diddy Kong Racing no Xbox 360, portado por Matherzin.",
                     "Diddy Kong Racing en la Xbox 360, portado por Matherzin.")),
              Info(L("PC port", "Port de PC", "Port de PC"), "Golden Balloon by akratch",
                   L("The native PC port this one is built on (github.com/akratch/goldenballoon).",
                     "O port nativo de PC em que este se baseia (github.com/akratch/goldenballoon).",
                     "El port nativo de PC en el que se basa este (github.com/akratch/goldenballoon).")),
              Info(L("Decompilation", "Decompilação", "Decompilación"), "DavidSM64 & contributors",
                   L("The Diddy Kong Racing decompilation the PC port compiles (github.com/DavidSM64/Diddy-Kong-Racing).",
                     "A decompilação do Diddy Kong Racing que o port de PC compila "
                     "(github.com/DavidSM64/Diddy-Kong-Racing).",
                     "La decompilación de Diddy Kong Racing que compila el port de PC "
                     "(github.com/DavidSM64/Diddy-Kong-Racing).")),
              Info(L("Toolchain", "Ferramentas", "Herramientas"), "xsdk",
                   L("The free Xbox 360 SDK that builds the game for the console.",
                     "O SDK livre de Xbox 360 que compila o jogo para o console.",
                     "El SDK libre de Xbox 360 que compila el juego para la consola.")),
              Info(L("Game", "Jogo", "Juego"), "Rare / Nintendo",
                   L("Diddy Kong Racing belongs to its owners. This port includes no game: use your own ROM.",
                     "Diddy Kong Racing pertence aos seus donos. Este port não inclui o jogo: use a sua ROM.",
                     "Diddy Kong Racing pertenece a sus dueños. Este port no incluye el juego: usa tu ROM.")),
          } });
    return sTabs;
}

std::string ValueText(const Item& item) {
    char buf[32];
    switch (item.kind) {
        case K_CHOICE: {
            const int idx = item.get ? item.get() : 0;
            return item.choices[std::clamp(idx, 0, (int)item.choices.size() - 1)].label;
        }
        case K_PERCENT:
            snprintf(buf, sizeof buf, "%d%%", (int)lroundf(Value(item.key).number));
            return buf;
        case K_INFO:
            if (item.id == ID_RESTART) {
                static const char* const kNames[3] = { "English", "Português (Brasil)", "Español" };
                const int want = Lang();
                const int have = dkr360_rom_language();
                std::string v = kNames[have];
                if (want != have) {
                    v += dkr360_rom_available(want) ? L(" (restart: A)", " (reiniciar: A)", " (reiniciar: A)")
                                                    : L(" (no ROM)", " (sem ROM)", " (sin ROM)");
                }
                return v;
            }
            return item.info;
        case K_ACTION:
            return "";
    }
    return "";
}

float ValueFraction(const Item& item) {
    if (item.kind == K_PERCENT) {
        return std::clamp(Value(item.key).number / 100.0f, 0.0f, 1.0f);
    }
    return -1.0f;
}

void Change(Item& item, int dir) {
    if (item.kind == K_CHOICE) {
        const int n = (int)item.choices.size();
        const int idx = ((item.get() + (dir == 0 ? 1 : dir)) % n + n) % n;
        item.set(idx);
    } else if (item.kind == K_PERCENT) {
        int v = (int)lroundf(Value(item.key).number);
        v = std::clamp(v + (dir == 0 ? 5 : dir * 5), 0, 100);
        SetInt(item.key, v);
    }
}

// ------------------------------------------------------------------------------------------------ pad

struct Pad {
    uint16_t held = 0, pressed = 0;
    bool up = false, down = false, left = false, right = false;
};

class PadReader {
  public:
    Pad Read(float dt) {
        Pad p;
        uint16_t held = 0;
        int lx = 0, ly = 0;
        for (uint32_t port = 0; port < 4; port++) {
            INPUT_STATE st = {};
            if (XamInputGetState(port, 0, &st) == 0) {
                held |= st.gamepad.buttons;
                if (std::abs((int)st.gamepad.thumb_lx) > std::abs(lx)) {
                    lx = st.gamepad.thumb_lx;
                }
                if (std::abs((int)st.gamepad.thumb_ly) > std::abs(ly)) {
                    ly = st.gamepad.thumb_ly;
                }
            }
        }
        p.held = held;
        p.pressed = held & ~mPrev;
        mPrev = held;
        const int dead = 18000;
        const bool dirs[4] = { (held & INPUT_GAMEPAD_DPAD_UP) || ly > dead, (held & INPUT_GAMEPAD_DPAD_DOWN) || ly < -dead,
                               (held & INPUT_GAMEPAD_DPAD_LEFT) || lx < -dead,
                               (held & INPUT_GAMEPAD_DPAD_RIGHT) || lx > dead };
        bool fire[4];
        for (int i = 0; i < 4; i++) {
            fire[i] = false;
            if (!dirs[i]) {
                mHeldTime[i] = -1.0f;
                continue;
            }
            if (mHeldTime[i] < 0.0f) {
                fire[i] = true;
                mHeldTime[i] = 0.0f;
                mRepeat[i] = 0.38f;
            } else {
                mHeldTime[i] += dt;
                if (mHeldTime[i] >= mRepeat[i]) {
                    fire[i] = true;
                    mRepeat[i] += 0.075f;
                }
            }
        }
        p.up = fire[0];
        p.down = fire[1];
        p.left = fire[2];
        p.right = fire[3];
        return p;
    }

  private:
    uint16_t mPrev = 0xFFFF; // nothing counts as pressed on the first frame
    float mHeldTime[4] = { -1, -1, -1, -1 };
    float mRepeat[4] = {};
};

// ------------------------------------------------------------------------------------------------ drawing helpers

ImU32 Col(int r, int g, int b, float a) {
    return IM_COL32(r, g, b, (int)(std::clamp(a, 0.0f, 1.0f) * 255.0f));
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

ImFont* sFont = nullptr;

void ButtonGlyph(ImDrawList* dl, ImVec2 c, float r, const char* letter, ImU32 col, float alpha) {
    dl->AddCircleFilled(c, r, col, 24);
    dl->AddCircle(c, r, Col(255, 255, 255, 0.35f * alpha), 24, 1.5f);
    const float size = r * 1.25f;
    const ImVec2 ts = sFont->CalcTextSizeA(size, FLT_MAX, 0.0f, letter);
    dl->AddText(sFont, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), Col(255, 255, 255, alpha), letter);
}

void ShoulderGlyph(ImDrawList* dl, ImVec2 c, const char* label, float alpha) {
    const float size = 17.0f;
    const ImVec2 ts = sFont->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
    const ImVec2 a(c.x - ts.x * 0.5f - 9, c.y - 13), b(c.x + ts.x * 0.5f + 9, c.y + 13);
    dl->AddRectFilled(a, b, Col(84, 60, 40, 0.95f * alpha), 8.0f);
    dl->AddRect(a, b, Col(255, 255, 255, 0.35f * alpha), 8.0f, 0, 1.5f);
    dl->AddText(sFont, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), Col(255, 245, 230, alpha), label);
}

float Text(ImDrawList* dl, float size, ImVec2 pos, ImU32 col, const char* s) {
    dl->AddText(sFont, size, pos, col, s);
    return sFont->CalcTextSizeA(size, FLT_MAX, 0.0f, s).x;
}

ImVec2 Measure(float size, const char* s) {
    return sFont->CalcTextSizeA(size, FLT_MAX, 0.0f, s);
}

// ------------------------------------------------------------------------------------------------ the menu

class ConsoleMenu {
  public:
    void Tick(float dt) {
        Pad pad = mPad.Read(dt);
        if (pad.pressed & INPUT_GAMEPAD_BACK) {
            if (mOpen) {
                Close();
            } else {
                Open();
            }
        } else if (mOpen) {
            HandleInput(pad);
        }
        // After closing, the game sees no input until every button is let go (the B that closed the menu, say).
        if (!mOpen && mBlocked && (pad.held & ~INPUT_GAMEPAD_BACK) == 0) {
            mBlocked = false;
        }
    }

    bool WantsInput() const {
        return mOpen || mBlocked;
    }

    void Draw(float dt) {
        mFade = std::clamp(mFade + (mOpen ? dt : -dt) * 7.0f, 0.0f, 1.0f);
        if (sShowFps) {
            DrawFps();
        }
        if (mFade > 0.0f) {
            DrawMenu(dt);
        }
        if (mToastTime > 0.0f) {
            mToastTime -= dt;
            DrawToast();
        }
    }

    void Open() {
        mOpen = true;
        mConfirm = false;
        mBlocked = true;
    }

    void SetTab(int tab) {
        mTab = std::clamp(tab, 0, (int)Tabs().size() - 1);
        mRow = 0;
    }

  private:
    void Close() {
        mOpen = false;
        mConfirm = false;
    }

    std::vector<Item>& Items() {
        return Tabs()[mTab].items;
    }

    void HandleInput(const Pad& pad) {
        std::vector<Item>& items = Items();
        if (mConfirm) {
            if (pad.left || pad.right) {
                mConfirmYes = !mConfirmYes;
            }
            if (pad.pressed & INPUT_GAMEPAD_A) {
                mConfirm = false;
                if (mConfirmYes) {
                    Item& item = items[mRow];
                    if (item.id == ID_RESTART) {
                        dkr360_restart();
                    } else {
                        item.run();
                    }
                    if (item.id == ID_DEFAULTS) {
                        Toast(L("Options restored to their defaults", "Opções restauradas para o padrão",
                                "Opciones restauradas a sus valores"));
                    }
                }
            } else if (pad.pressed & INPUT_GAMEPAD_B) {
                mConfirm = false;
            }
            return;
        }
        if (pad.pressed & INPUT_GAMEPAD_B) {
            Close();
            return;
        }
        const int tabs = (int)Tabs().size();
        if (pad.pressed & INPUT_GAMEPAD_LEFT_SHOULDER) {
            mTab = (mTab + tabs - 1) % tabs;
            mRow = 0;
            mScroll = 0.0f;
        } else if (pad.pressed & INPUT_GAMEPAD_RIGHT_SHOULDER) {
            mTab = (mTab + 1) % tabs;
            mRow = 0;
            mScroll = 0.0f;
        }
        std::vector<Item>& now = Items();
        const int rows = (int)now.size();
        if (pad.up) {
            mRow = (mRow + rows - 1) % rows;
        } else if (pad.down) {
            mRow = (mRow + 1) % rows;
        }
        Item& item = now[mRow];
        if (item.id == ID_RESTART) {
            if ((pad.pressed & INPUT_GAMEPAD_A) && Lang() != dkr360_rom_language() &&
                dkr360_rom_available(Lang())) {
                mConfirm = true;
                mConfirmYes = true;
            }
            return;
        }
        if (item.kind == K_ACTION) {
            if (pad.pressed & INPUT_GAMEPAD_A) {
                if (item.confirm) {
                    mConfirm = true;
                    mConfirmYes = false;
                } else {
                    item.run();
                }
            }
        } else if (item.kind != K_INFO) {
            const bool language = item.id == ID_LANGUAGE;
            if (pad.left) {
                Change(item, -1);
            } else if (pad.right) {
                Change(item, 1);
            } else if (pad.pressed & INPUT_GAMEPAD_A) {
                Change(item, 0);
            }
            (void)language;
        }
    }

    void Toast(const char* s) {
        mToast = s;
        mToastTime = 2.0f;
    }

    void DrawFps() {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        char buf[32];
        snprintf(buf, sizeof buf, "%.0f FPS", dkr360_video_fps());
        const ImVec2 ts = Measure(20.0f, buf);
        const ImVec2 a(1280 - ts.x - 72, 30), b(1280 - 52, 30 + ts.y + 10);
        dl->AddRectFilled(a, b, Col(24, 12, 6, 0.6f), 6.0f);
        Text(dl, 20.0f, ImVec2(a.x + 10, a.y + 5), Col(150, 230, 160, 1.0f), buf);
    }

    void DrawToast() {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const float a = std::clamp(mToastTime * 2.0f, 0.0f, 1.0f);
        const ImVec2 ts = Measure(24.0f, mToast);
        const ImVec2 p0(640 - ts.x * 0.5f - 24, 600), p1(640 + ts.x * 0.5f + 24, 600 + ts.y + 20);
        dl->AddRectFilled(p0, p1, Col(52, 30, 14, 0.92f * a), 12.0f);
        dl->AddRect(p0, p1, Col(255, 190, 60, 0.8f * a), 12.0f, 0, 2.0f);
        Text(dl, 24.0f, ImVec2(p0.x + 24, p0.y + 10), Col(255, 255, 255, a), mToast);
    }

    void DrawMenu(float dt) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const float a = mFade;
        const float ease = 1.0f - (1.0f - a) * (1.0f - a);
        const float W = 1280.0f, H = 720.0f;

        // Dim the game behind.
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), Col(8, 4, 0, 0.55f * a));

        const float pw = 900.0f, ph = 580.0f;
        const float px = (W - pw) * 0.5f, py = (H - ph) * 0.5f + (1.0f - ease) * 24.0f;
        const ImVec2 p0(px, py), p1(px + pw, py + ph);

        // Panel: a warm jungle-wood gradient, a golden border, a banana-yellow stripe under the header.
        dl->AddRectFilled(ImVec2(p0.x + 6, p0.y + 10), ImVec2(p1.x + 6, p1.y + 10), Col(0, 0, 0, 0.45f * a), 18.0f);
        dl->AddRectFilledMultiColor(p0, p1, Col(70, 38, 18, 0.97f * a), Col(70, 38, 18, 0.97f * a),
                                    Col(26, 14, 8, 0.97f * a), Col(26, 14, 8, 0.97f * a));
        dl->AddRect(p0, p1, Col(255, 190, 60, 0.6f * a), 18.0f, 0, 2.0f);

        // Header: a little balloon before the title.
        const float hx = p0.x + 36, hy = p0.y + 26;
        dl->AddCircleFilled(ImVec2(hx + 14, hy + 16), 14.0f, Col(230, 40, 40, a), 24);
        dl->AddCircleFilled(ImVec2(hx + 9, hy + 10), 4.0f, Col(255, 160, 160, 0.8f * a), 12);
        dl->AddTriangleFilled(ImVec2(hx + 10, hy + 31), ImVec2(hx + 18, hy + 31), ImVec2(hx + 14, hy + 36),
                              Col(230, 40, 40, a));
        dl->AddLine(ImVec2(hx + 14, hy + 36), ImVec2(hx + 12, hy + 52), Col(240, 230, 210, 0.8f * a), 1.5f);
        Text(dl, 38.0f, ImVec2(hx + 40, hy - 2), Col(255, 220, 70, a), "DIDDY KONG RACING");
        Text(dl, 20.0f, ImVec2(hx + 42, hy + 40), Col(235, 200, 150, a), L("Xbox 360 options", "Opções do Xbox 360", "Opciones de Xbox 360"));
        {
            const char* by = L("Port by Matherzin", "Port por Matherzin", "Port por Matherzin");
            const ImVec2 bs = Measure(18.0f, by);
            Text(dl, 18.0f, ImVec2(p1.x - 36 - bs.x, hy + 44), Col(255, 190, 60, 0.9f * a), by);
        }
        dl->AddRectFilledMultiColor(ImVec2(p0.x + 30, p0.y + 96), ImVec2(p1.x - 30, p0.y + 99),
                                    Col(255, 210, 40, 0.9f * a), Col(255, 210, 40, 0.0f), Col(255, 210, 40, 0.0f),
                                    Col(255, 210, 40, 0.9f * a));

        // Tabs.
        std::vector<Tab>& tabs = Tabs();
        float tx = p0.x + 92;
        const float ty = p0.y + 116;
        ShoulderGlyph(dl, ImVec2(p0.x + 54, ty + 18), "LB", a);
        for (int i = 0; i < (int)tabs.size(); i++) {
            const ImVec2 ts = Measure(22.0f, tabs[i].label);
            const ImVec2 t0(tx, ty), t1(tx + ts.x + 32, ty + 36);
            if (i == mTab) {
                mTabX = mTabX < 0 ? t0.x : Lerp(mTabX, t0.x, std::min(1.0f, dt * 16.0f));
                mTabW = mTabW < 0 ? (t1.x - t0.x) : Lerp(mTabW, t1.x - t0.x, std::min(1.0f, dt * 16.0f));
            }
            tx = t1.x + 8;
        }
        dl->AddRectFilled(ImVec2(mTabX, ty), ImVec2(mTabX + mTabW, ty + 36), Col(220, 120, 30, 0.9f * a), 18.0f);
        tx = p0.x + 92;
        for (int i = 0; i < (int)tabs.size(); i++) {
            const ImVec2 ts = Measure(22.0f, tabs[i].label);
            Text(dl, 22.0f, ImVec2(tx + 16, ty + 18 - ts.y * 0.5f),
                 i == mTab ? Col(255, 255, 255, a) : Col(220, 190, 150, a), tabs[i].label);
            tx += ts.x + 32 + 8;
        }
        ShoulderGlyph(dl, ImVec2(std::min(tx + 30, p1.x - 40), ty + 18), "RB", a);

        // Rows.
        std::vector<Item>& items = Items();
        const float listTop = p0.y + 172, listBottom = p1.y - 132;
        const float rowH = 46.0f;
        const float visible = listBottom - listTop;
        const float wanted = std::clamp(mRow * rowH - visible * 0.5f + rowH * 0.5f, 0.0f,
                                        std::max(0.0f, items.size() * rowH - visible));
        mScroll = Lerp(mScroll, wanted, std::min(1.0f, dt * 14.0f));
        const float selY = listTop + mRow * rowH - mScroll;
        mSelY = mSelY < 0 ? selY : Lerp(mSelY, selY, std::min(1.0f, dt * 20.0f));

        dl->PushClipRect(ImVec2(p0.x + 20, listTop - 2), ImVec2(p1.x - 20, listBottom + 2), true);
        {
            const ImVec2 b0(p0.x + 30, mSelY + 2), b1(p1.x - 30, mSelY + rowH - 2);
            dl->AddRectFilledMultiColor(b0, b1, Col(255, 160, 40, 0.38f * a), Col(255, 160, 40, 0.08f * a),
                                        Col(255, 160, 40, 0.08f * a), Col(255, 160, 40, 0.38f * a));
            dl->AddRectFilled(b0, ImVec2(b0.x + 5, b1.y), Col(255, 220, 70, a), 2.0f);
        }
        for (int i = 0; i < (int)items.size(); i++) {
            const Item& item = items[i];
            const float y = listTop + i * rowH - mScroll;
            if (y + rowH < listTop || y > listBottom) {
                continue;
            }
            const bool sel = i == mRow;
            const ImU32 labelCol = sel ? Col(255, 255, 255, a) : Col(240, 222, 196, a);
            const ImVec2 ls = Measure(23.0f, item.label);
            Text(dl, 23.0f, ImVec2(p0.x + 52, y + rowH * 0.5f - ls.y * 0.5f), labelCol, item.label);

            const float right = p1.x - 52;
            if (item.kind == K_ACTION) {
                if (sel) {
                    ButtonGlyph(dl, ImVec2(right - 12, y + rowH * 0.5f), 13.0f, "A", Col(60, 170, 70, 1.0f), a);
                }
                continue;
            }
            const std::string v = ValueText(item);
            const ImVec2 vs = Measure(22.0f, v.c_str());
            if (item.kind == K_INFO) {
                Text(dl, 22.0f, ImVec2(right - vs.x, y + rowH * 0.5f - vs.y * 0.5f), Col(255, 220, 70, a), v.c_str());
                continue;
            }
            const float frac = ValueFraction(item);
            float vx = right - vs.x;
            if (sel) {
                vx -= 22;
                const float cy = y + rowH * 0.5f;
                dl->AddTriangleFilled(ImVec2(right - 10, cy - 7), ImVec2(right, cy), ImVec2(right - 10, cy + 7),
                                      Col(255, 200, 70, a));
                const float lx = vx - 22;
                dl->AddTriangleFilled(ImVec2(lx + 10, cy - 7), ImVec2(lx, cy), ImVec2(lx + 10, cy + 7),
                                      Col(255, 200, 70, a));
            }
            if (frac >= 0.0f) {
                const float bw = 150.0f;
                const float bx = vx - (sel ? 40 : 18) - bw, by = y + rowH * 0.5f - 4;
                dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + 8), Col(80, 52, 30, a), 4.0f);
                dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * frac, by + 8), Col(255, 190, 60, a), 4.0f);
            }
            Text(dl, 22.0f, ImVec2(vx, y + rowH * 0.5f - vs.y * 0.5f),
                 sel ? Col(255, 220, 70, a) : Col(235, 200, 150, a), v.c_str());
        }
        dl->PopClipRect();

        if (items.size() * rowH > visible) {
            const float track = visible;
            const float thumb = std::max(30.0f, track * visible / (items.size() * rowH));
            const float t = mScroll / std::max(1.0f, items.size() * rowH - visible);
            const float sx = p1.x - 24;
            dl->AddRectFilled(ImVec2(sx, listTop), ImVec2(sx + 4, listBottom), Col(80, 52, 30, a), 2.0f);
            dl->AddRectFilled(ImVec2(sx, listTop + (track - thumb) * t),
                              ImVec2(sx + 4, listTop + (track - thumb) * t + thumb), Col(255, 190, 60, a), 2.0f);
        }

        // Description of the selected row.
        dl->AddLine(ImVec2(p0.x + 30, p1.y - 122), ImVec2(p1.x - 30, p1.y - 122), Col(255, 190, 60, 0.25f * a), 1.0f);
        const char* desc = items[mRow].desc;
        if (desc && desc[0] != 0) {
            dl->AddText(sFont, 20.0f, ImVec2(p0.x + 40, p1.y - 110), Col(235, 210, 175, a), desc, nullptr,
                        pw - 80.0f);
        }

        // Button hints.
        float hxp = p0.x + 40;
        const float hyp = p1.y - 38;
        ButtonGlyph(dl, ImVec2(hxp + 13, hyp), 13.0f, "A", Col(60, 170, 70, 1.0f), a);
        hxp += 34 + Text(dl, 20.0f, ImVec2(hxp + 32, hyp - 12), Col(245, 232, 215, a), L("Change", "Mudar", "Cambiar")) + 28;
        ButtonGlyph(dl, ImVec2(hxp + 13, hyp), 13.0f, "B", Col(200, 50, 50, 1.0f), a);
        hxp += 34 + Text(dl, 20.0f, ImVec2(hxp + 32, hyp - 12), Col(245, 232, 215, a), L("Back", "Voltar", "Volver")) + 28;
        ShoulderGlyph(dl, ImVec2(hxp + 22, hyp), "LB", a);
        ShoulderGlyph(dl, ImVec2(hxp + 70, hyp), "RB", a);
        hxp += 100 + Text(dl, 20.0f, ImVec2(hxp + 98, hyp - 12), Col(245, 232, 215, a), L("Tabs", "Abas", "Pestañas")) + 28;
        ShoulderGlyph(dl, ImVec2(hxp + 34, hyp), "BACK", a);
        Text(dl, 20.0f, ImVec2(hxp + 72, hyp - 12), Col(245, 232, 215, a), L("Close", "Fechar", "Cerrar"));

        if (mConfirm) {
            DrawConfirm(dl, a);
        }
    }

    void DrawConfirm(ImDrawList* dl, float a) {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(1280, 720), Col(0, 0, 0, 0.5f * a));
        const ImVec2 c0(440, 270), c1(840, 450);
        dl->AddRectFilledMultiColor(c0, c1, Col(80, 44, 20, a), Col(80, 44, 20, a), Col(34, 18, 10, a),
                                    Col(34, 18, 10, a));
        dl->AddRect(c0, c1, Col(255, 190, 60, 0.8f * a), 14.0f, 0, 2.0f);
        const char* title = Items()[mRow].label;
        const ImVec2 ts = Measure(26.0f, title);
        Text(dl, 26.0f, ImVec2(640 - ts.x * 0.5f, 296), Col(255, 220, 70, a), title);
        const char* question = Items()[mRow].id == ID_RESTART
                                   ? L("Restart the game now?", "Reiniciar o jogo agora?", "¿Reiniciar el juego ahora?")
                                   : L("Are you sure?", "Tem certeza?", "¿Seguro?");
        const ImVec2 qs = Measure(22.0f, question);
        Text(dl, 22.0f, ImVec2(640 - qs.x * 0.5f, 340), Col(245, 232, 215, a), question);
        for (int i = 0; i < 2; i++) {
            const bool yes = i == 0;
            const char* s = yes ? L("Yes", "Sim", "Sí") : L("No", "Não", "No");
            const ImVec2 b0(yes ? 500 : 660, 390), b1(yes ? 620 : 780, 430);
            const bool sel = yes == mConfirmYes;
            dl->AddRectFilled(b0, b1, sel ? Col(220, 120, 30, a) : Col(80, 52, 30, a), 10.0f);
            const ImVec2 ss = Measure(22.0f, s);
            Text(dl, 22.0f, ImVec2((b0.x + b1.x) * 0.5f - ss.x * 0.5f, 410 - ss.y * 0.5f), Col(255, 255, 255, a), s);
        }
    }

    PadReader mPad;
    bool mOpen = false, mBlocked = false, mConfirm = false, mConfirmYes = false;
    int mTab = 0, mRow = 0;
    float mFade = 0.0f, mScroll = 0.0f, mSelY = -1.0f, mTabX = -1.0f, mTabW = -1.0f;
    const char* mToast = "";
    float mToastTime = 0.0f;
};

ConsoleMenu sMenu;
bool sReady = false;
std::chrono::steady_clock::time_point sLast;

// Dear ImGui's textures (the font atlas) into Xenos: ImGui 1.92 asks the renderer to create and update them.
void UpdateTextures(ImDrawData* data) {
    if (!data->Textures) {
        return;
    }
    for (ImTextureData* tex : *data->Textures) {
        if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
            // Alpha8 (asked for in Init): white with the glyphs' coverage in alpha, as bytes R G B A. The RGBA32 form
            // packs ImU32s, whose bytes come out the other way round on this big-endian CPU.
            std::vector<uint8_t> rgba((size_t)tex->Width * tex->Height * 4);
            const uint8_t* src = (const uint8_t*)tex->GetPixels();
            for (size_t i = 0; i < (size_t)tex->Width * tex->Height; i++) {
                const uint8_t v = tex->Format == ImTextureFormat_Alpha8 ? src[i] : src[i * 4 + 3];
                rgba[i * 4 + 0] = 255;
                rgba[i * 4 + 1] = 255;
                rgba[i * 4 + 2] = 255;
                rgba[i * 4 + 3] = v;
            }
            const uint32_t id = dkr360_ui_texture(rgba.data(), tex->Width, tex->Height);
            tex->SetTexID((ImTextureID)id);
            tex->SetStatus(ImTextureStatus_OK);
            xsdk_log_write("dkr360 menu: font atlas %dx%d", tex->Width, tex->Height);
        } else if (tex->Status == ImTextureStatus_WantDestroy) {
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

void RenderDrawData(ImDrawData* data) {
    UpdateTextures(data);
    for (int n = 0; n < data->CmdListsCount; n++) {
        const ImDrawList* list = data->CmdLists[n];
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback || cmd.ElemCount == 0) {
                continue;
            }
            const int x0 = (int)std::max(cmd.ClipRect.x, 0.0f), y0 = (int)std::max(cmd.ClipRect.y, 0.0f);
            const int x1 = (int)std::min(cmd.ClipRect.z, 1280.0f), y1 = (int)std::min(cmd.ClipRect.w, 720.0f);
            if (x1 <= x0 || y1 <= y0) {
                continue;
            }
            dkr360_ui_draw((uint32_t)cmd.GetTexID(), list->VtxBuffer.Data + cmd.VtxOffset,
                           (uint32_t)(list->VtxBuffer.Size - cmd.VtxOffset), list->IdxBuffer.Data + cmd.IdxOffset,
                           cmd.ElemCount, x0, y0, x1 - x0, y1 - y0);
        }
    }
}

void Init() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.BackendRendererName = "dkr360-xenos";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    io.Fonts->TexDesiredFormat = ImTextureFormat_Alpha8;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    // Roboto Medium (Apache License 2.0, from Dear ImGui's misc/fonts)
    sFont = io.Fonts->AddFontFromMemoryCompressedBase85TTF(dkr360_roboto_compressed_data_base85, 30.0f, &cfg);
    LoadConsoleSettings();
    // A run nobody holds a pad in (Xenia): the menu open on the tab in game:\dkr360_menu_open.txt, for a capture.
    if (FILE* f = fopen("dkr360_menu_open.txt", "r")) {
        int tab = 0;
        if (fscanf(f, "%d", &tab) != 1) {
            tab = 0;
        }
        fclose(f);
        sMenu.SetTab(tab);
        sMenu.Open();
    }
    sReady = true;
    xsdk_log_write("dkr360 menu: ready");
}

// The first run's settings that differ from Golden Balloon's PC defaults: 60 frames (interpolated), the HUD at the
// screen's edges and a master volume a little under full (the mix clips at 100% in the loudest races). Once, on the
// first frame: Golden Balloon then keeps them in dkr360\video.ini.
void FirstRunDefaults() {
    static bool sDone = false;
    if (sDone) {
        return;
    }
    sDone = true;
    FILE* mark = fopen("dkr360/defaults-applied", "r");
    if (mark) {
        fclose(mark);
        return;
    }
    SetFrameRate(0);
    SetInt(MDKR_AUDIO_MASTER_VOLUME, 85);
    SetInt(MDKR_VIDEO_WIDESCREEN_HUD, 1);
    if ((mark = fopen("dkr360/defaults-applied", "w"))) {
        fputs("1\n", mark);
        fclose(mark);
    }
    xsdk_log_write("dkr360 menu: first run, 60 fps and master volume 85%% set");
}

} // namespace

extern "C" {

int dkr360_menu_wants_input(void) {
    return sReady && sMenu.WantsInput();
}

// End of each rendered frame (gfx_xenos.cpp): the pad, then the menu over the picture.
void dkr360_menu_render(void) {
    if (!sReady) {
        Init();
    }
    FirstRunDefaults();
    {
        // A run nobody holds a pad in (Xenia): with game:\dkr360_test_restart.txt the menu's restart runs once, at
        // frame 300, the file removed first.
        static unsigned sFrames;
        if (++sFrames == 300) {
            if (FILE* f = fopen("dkr360_test_restart.txt", "r")) {
                fclose(f);
                remove("dkr360_test_restart.txt");
                dkr360_restart();
            }
        }
    }
    if (sRebuildTabs) {
        sRebuildTabs = false;
        sTabs.clear();
    }
    const auto now = std::chrono::steady_clock::now();
    float dt = sLast.time_since_epoch().count() == 0 ? 1.0f / 60.0f
                                                     : std::chrono::duration<float>(now - sLast).count();
    sLast = now;
    dt = std::clamp(dt, 0.0001f, 0.1f);
    sMenu.Tick(dt);
    {
        // Which controllers XAM and SDL see, when that changes (the game reads its pads through SDL).
        static int sLastMask = -1, sLastSdl = -1;
        int mask = 0;
        for (uint32_t port = 0; port < 4; port++) {
            INPUT_STATE st = {};
            if (XamInputGetState(port, 0, &st) == 0) {
                mask |= 1 << port;
            }
        }
        const int sdl = SDL_NumJoysticks();
        if (mask != sLastMask || sdl != sLastSdl) {
            xsdk_log_write("dkr360: controllers on XAM ports %c%c%c%c, SDL sees %d", (mask & 1) ? '1' : '-',
                           (mask & 2) ? '2' : '-', (mask & 4) ? '3' : '-', (mask & 8) ? '4' : '-', sdl);
            sLastMask = mask;
            sLastSdl = sdl;
        }
    }
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = dt;
    ImGui::NewFrame();
    sMenu.Draw(dt);
    ImGui::Render();
    RenderDrawData(ImGui::GetDrawData());
}

void dkr360_settings_load(void) {
}

} // extern "C"
