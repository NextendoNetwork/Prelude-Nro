// ============================================================
//  Nextendo .nro — thème UI (style Nimbus/Pretendo, marque rouge+bleu).
// ============================================================
#ifndef UI_THEME_H
#define UI_THEME_H
#include <switch.h>

typedef struct { u8 r, g, b, a; } Color;
#define COL(R, G, B) ((Color){ (R), (G), (B), 255 })

#define C_RED      COL(0xE4, 0x00, 0x14)  // rouge marque (sélection Nintendo)
#define C_BLUE     COL(0x1C, 0xA9, 0xE0)  // bleu marque (sélection Nextendo)
#define C_BG       COL(0x12, 0x14, 0x19)  // fond
#define C_TITLE    COL(0xFF, 0xFF, 0xFF)  // titre / noms
#define C_SUBTLE   COL(0x8C, 0x93, 0x9F)  // texte secondaire / aide
#define C_CARD     COL(0x1E, 0x22, 0x2B)  // carte
#define C_CARD_SEL COL(0x26, 0x2C, 0x38)  // carte sélectionnée
#define C_GREEN    COL(0x36, 0xCE, 0x73)  // badge "CHARGÉ" / succès
#define C_S2       COL(0xF0, 0x2D, 0x7D)  // rose Splatoon (bouton planning S2)

#define FB_W 1280
#define FB_H 720
#define CARD_W 300
#define CARD_H 300
#define CARD_GAP 80
#define CARD_Y 170
#define CARD0_X ((FB_W - (2 * CARD_W + CARD_GAP)) / 2)  // gauche (Nextendo)
#define CARD1_X (CARD0_X + CARD_W + CARD_GAP)           // droite (Nintendo)

// Barre "Splatoon 2 — Planning en ligne" sous les deux cartes de mode.
#define S2BAR_W 900
#define S2BAR_H 64
#define S2BAR_X ((FB_W - S2BAR_W) / 2)
#define S2BAR_Y 500

#define CHOICE_NEXTENDO 0
#define CHOICE_NINTENDO 1

// Focus de l'ecran de bascule : la paire de cartes (mode) ou la barre S2.
#define FOCUS_MODE 0
#define FOCUS_S2   1

#endif // UI_THEME_H
