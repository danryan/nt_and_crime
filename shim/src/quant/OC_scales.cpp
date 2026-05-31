// Shim port of vendor OC_scales.cpp. Omits SaveToScala and LoadScala
// (dead code on NT: no SD card). Omits avr/pgmspace.h and FLASHMEM
// (not applicable on Cortex-M7 / host sim). Omits the static_assert on
// scale_names count: those string tables are not ported to the shim.
#include "OC_scales.h"
#include "util/util_macros.h"
#include <cstring>

namespace OC {

Scale user_scales[Scales::SCALE_USER_COUNT];
Scale dummy_scale;

void Scales::Init() {
  for (size_t i = 0; i < SCALE_USER_COUNT; ++i)
    memcpy(&user_scales[i], &braids::scales[1], sizeof(Scale));
}

void Scales::Validate() {
  for (size_t i = 0; i < SCALE_USER_COUNT; ++i) {
    CONSTRAIN(user_scales[i].num_notes, 4, 16);
    CONSTRAIN(user_scales[i].span, 12 << 7, 24 << 7);
  }
}

const Scale &Scales::GetScale(int index) {
  CONSTRAIN(index, 0, NUM_SCALES - 1);
  if (index < SCALE_USER_COUNT)
    return user_scales[index];
  else
    return braids::scales[index - SCALE_USER_COUNT];
}

// scale_names is the full vendor label table (vendor OC_scales.cpp:259,
// verbatim). It is ported because scale-selecting apps use it both as a setting
// value_attr value_names (so the NT enum parameter renders scale labels across
// the full range) and in their customUI menus; the prior {nullptr} stub read
// out of bounds for any scale index above 0. Entry count equals NUM_SCALES
// (SCALE_USER_COUNT + the vendor braids::scales table the shim links), so a flat
// value_names walk over [min, max] stays in bounds.
const char* const scale_names[] = {
    "User-defined 1",
    "User-defined 2",
    "User-defined 3",
    "User-defined 4",
    "Off ",
    "Semitone",
    "Ionian",
    "Dorian",
    "Phrygian",
    "Lydian",
    "Mixolydian",
    "Aeolian",
    "Locrian",
    "Blues major",
    "Blues minor",
    "Pentatonic maj",
    "Pentatonic min",
    "Folk",
    "Japanese",
    "Gamelan",
    "Gypsy",
    "Arabian",
    "Flamenco",
    "Whole tone",
    "Pythagorean",
    "EB/4",
    "E /4",
    "EA/4",
    "Bhairav",
    "Gunakri",
    "Marwa",
    "Shree [Camel]",
    "Purvi",
    "Bilawal",
    "Yaman",
    "Kafi",
    "Bhimpalasree",
    "Darbari",
    "Rageshree",
    "Khamaj",
    "Mimal",
    "Parameshwari",
    "Rangeshwari",
    "Gangeshwari",
    "Kameshwari",
    "Pa Khafi",
    "Natbhairav",
    "Malkauns",
    "Bairagi",
    "B Todi",
    "Chandradeep",
    "Kaushik Todi",
    "Jogeshwari",
    "Tart.-Vallotti",
    "13of22tETgen=5",
    "Mandelbaum",
    "Magic-in-145tET",
    "Quartaminor3rds",
    "Armodue semi-eq",
    "Hirajoshi",
    "Scot bagpipes",
    "Thai ranat",
    "Sevish12on31EDO",
    "11tetMachine6",
    "13tetFather8",
    "15tetBlackwd10",
    "16tetMavila7",
    "16tetMavila9",
    "17tetSuprpyth12",
    "22tetOrwell9",
    "22tetPajaraSy10",
    "22tetPajara5-10",
    "22tetPorcupine7",
    "26tetFlattone12",
    "26tetLemba10",
    "46tetSensi11",
    "53tetOrwell9",
    "12of72tetRodgrs",
    "TrivalentZeus7",
    "202tetOctone",
    "313tetElfMadag9",
    "MarvelWooGlumma",
    "TOP Parapyth12",
    "16-ED (2 or 3)",
    "15-ED (2 or 3)",
    "14-ED (2 or 3)",
    "13-ED (2 or 3)",
    "11-ED (2 or 3)",
    "10-ED (2 or 3)",
    "9-ED (2 or 3)",
    "8-ED (2 or 3)",
    "7-ED (2 or 3)",
    "6-ED2",
    "5-ED2",
    "16-HD2",
    "15-HD2",
    "14-HD2",
    "13-HD2",
    "12-HD2",
    "11-HD2",
    "10-HD2",
    "9-HD2",
    "8-HD2",
    "7-HD2",
    "6-HD2",
    "5-HD2",
    "4-HD2",
    "16-SD2",
    "15-SD2",
    "14-SD2",
    "13-SD2",
    "12-SD2",
    "11-SD2",
    "10-SD2",
    "9-SD2",
    "8-SD2",
    "7-SD2",
    "6-SD2",
    "5-SD2",
    "4-SD2",
    "Bohlen-Pierce =",
    "Bohlen-Pierce j",
    "Bohlen-Pierce l",
    "8-24-HD3[16]",
    "7-21-HD3[14]",
    "6-18-HD3[12]",
    "5-15-HD3[10]",
    "4-12-HD3[8]",
    "24-8-SD3[16]",
    "21-7-SD3[14]",
    "18-6-SD3[12]",
    "15-5-SD3[10]",
    "12-4-SD3[8]",
    "5th+7th",
    "5th+6th",
    "Triad min+7",
    "Triad maj+7",
    "Triad min+6",
    "Triad maj+6",
    "Fifth",
    "TriadMaj",
    "TriadMin",
    "HarmonicMin",
    "Locrian n6",
    "Ionian aug",
    "Misheberakh",
    "Freygish",
    "Lydian #9",
    "Ultralocrian",
};
// scale_names_short is the full vendor short-label table (vendor OC_scales.cpp:101,
// verbatim). ASR's SCALE setting uses it as its enum value_names, so the prior
// {nullptr} stub read out of bounds. Entry count matches scale_names (>= NUM_SCALES).
const char* const scale_names_short[] = {
    "USR1", "USR2", "USR3", "USR4",
    "OFF ", "SEMI", "IONI", "DORI", "PHRY", "LYDI", "MIXO", "AEOL", "LOCR",
    "BLU+", "BLU-", "PEN+", "PEN-", "FOLK", "JAPA", "GAME", "GYPS", "ARAB",
    "FLAM", "WHOL", "PYTH", "EB/4", "E /4", "EA/4", "BHAI", "GUNA", "MARW",
    "SHRI", "PURV", "BILA", "YAMA", "KAFI", "BHIM", "DARB", "RAGE", "KHAM",
    "MIMA", "PARA", "RANG", "GANG", "KAME", "PAKA", "NATB", "KAUN", "BAIR",
    "BTOD", "CHAN", "KTOD", "JOGE",
    "VALL", "1322", "MANF", "MAGC", "QUAR", "ARMO", "HIRA", "SCOT", "THAI",
    "SEVI", "MACH", "FATH", "BLAC", "MAV7", "MAV9", "SPYT", "22OR", "PAJS",
    "PAJP", "PORC", "FLAT", "LEMB", "SENS", "53OR", "1272", "TRIZ", "2028",
    "MADG", "MARV", "PARA",
    "16ED", "15ED", "14ED", "13ED", "11ED", "10ED", "9ED", "8ED", "7ED",
    "6ED", "5ED", "16H2", "15H2", "14H2", "13H2", "12H2", "11H2", "10H2",
    "9H2", "8H2", "7H2", "6H2", "5H2", "4H2", "16S2", "15S2", "14S2", "13S2",
    "12S2", "11S2", "10S2", "9S2", "8S2", "7S2", "6S2", "5S2", "4S2",
    "B-Pe", "B-Pj", "B-Pl", "16H3", "14H3", "12H3", "10H3", "8H3", "16S3",
    "14S3", "12S3", "10S3", "8S3",
    "5+7", "5+6", "3b7-", "3b7+", "3b6-", "3b6+", "5th", "3b+", "3b-", "HAR-",
    "LOn6", "IAUG", "MBKH", "FREY", "LY#9", "UTLO",
};
// voltage_scalings stays omitted: the NT shim applies no per-output voltage
// scaling. nullptr stub satisfies the extern.
const char* const voltage_scalings[] = { nullptr };

}; // namespace OC
