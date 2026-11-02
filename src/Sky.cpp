// Tag und Nacht: Sonne, Mond und Sterne aus der Uhrzeit (siehe Sky.h)
#include "Sky.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace sky {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kObliquity = 23.44;      // Schiefe der Ekliptik (Grad)
constexpr double kSunLongitude = 31.5;    // Ort der Sonne auf der Ekliptik (Grad): Ende April
constexpr double kMoonBehind = 205.0;     // Mond so weit hinter der Sonne (Grad Rektaszension): fast voll, geht nach ihr auf
constexpr double kMoonDec = 0.0;          // Deklination des Monds (Grad)

// Stern im Katalog: Rektaszension (Stunden, Minuten), Deklination (Grad), Größenklasse, Farbe
struct StarDef {
    const char* name;
    double raH, raM;
    double dec;
    float mag;
    int color;
};
constexpr double D(double deg, double min) { return deg < 0 ? deg - min / 60.0 : deg + min / 60.0; }

// Die hellen Sterne der bekannten Sternbilder (J2000, auf etwa eine Bogenminute)
const StarDef kStars[] = {
    // Großer Wagen
    {"Dubhe", 11, 3.73, D(61, 45), 1.79f, 3}, {"Merak", 11, 1.84, D(56, 22), 2.37f, 1},
    {"Phecda", 11, 53.83, D(53, 41), 2.44f, 1}, {"Megrez", 12, 15.43, D(57, 1), 3.31f, 1},
    {"Alioth", 12, 54.03, D(55, 57), 1.77f, 1}, {"Mizar", 13, 23.93, D(54, 55), 2.27f, 1},
    {"Alcor", 13, 25.22, D(54, 59), 4.01f, 1}, {"Alkaid", 13, 47.54, D(49, 18), 1.86f, 0},
    // Kleiner Wagen
    {"Polaris", 2, 31.82, D(89, 15.9), 1.98f, 2}, {"Kochab", 14, 50.70, D(74, 9), 2.08f, 3},
    {"Pherkad", 15, 20.73, D(71, 50), 3.05f, 1}, {"Yildun", 17, 32.20, D(86, 35), 4.36f, 1},
    {"EpsUMi", 16, 45.97, D(82, 2), 4.21f, 2}, {"ZetaUMi", 15, 44.06, D(77, 47.7), 4.32f, 1},
    {"EtaUMi", 16, 17.50, D(75, 45), 4.95f, 2},
    // Kassiopeia
    {"Caph", 0, 9.18, D(59, 9), 2.28f, 2}, {"Schedar", 0, 40.51, D(56, 32), 2.24f, 3},
    {"GammaCas", 0, 56.71, D(60, 43), 2.15f, 0}, {"Ruchbah", 1, 25.82, D(60, 14), 2.66f, 1},
    {"Segin", 1, 54.40, D(63, 40), 3.35f, 0},
    // Orion
    {"Betelgeuse", 5, 55.17, D(7, 24), 0.45f, 4}, {"Rigel", 5, 14.54, D(-8, 12), 0.13f, 0},
    {"Bellatrix", 5, 25.13, D(6, 21), 1.64f, 0}, {"Saiph", 5, 47.76, D(-9, 40), 2.06f, 0},
    {"Alnitak", 5, 40.76, D(-1, 56), 1.77f, 0}, {"Alnilam", 5, 36.21, D(-1, 12), 1.69f, 0},
    {"Mintaka", 5, 32.01, -0.30, 2.23f, 0}, {"Meissa", 5, 35.14, D(9, 56), 3.39f, 0},
    // Großer und Kleiner Hund
    {"Sirius", 6, 45.15, D(-16, 43), -1.46f, 0}, {"Mirzam", 6, 22.70, D(-17, 57), 1.98f, 0},
    {"Wezen", 7, 8.39, D(-26, 24), 1.83f, 2}, {"Adhara", 6, 58.63, D(-28, 58), 1.50f, 0},
    {"Aludra", 7, 24.10, D(-29, 18), 2.45f, 0}, {"Procyon", 7, 39.30, D(5, 13.5), 0.34f, 2},
    {"Gomeisa", 7, 27.15, D(8, 17), 2.89f, 0},
    // Zwillinge
    {"Castor", 7, 34.60, D(31, 53), 1.58f, 1}, {"Pollux", 7, 45.32, D(28, 1.6), 1.14f, 3},
    {"Alhena", 6, 37.71, D(16, 23.9), 1.93f, 1}, {"Mebsuta", 6, 43.93, D(25, 7.9), 2.98f, 2},
    {"Tejat", 6, 22.96, D(22, 30.8), 2.87f, 4}, {"Wasat", 7, 20.12, D(21, 58.9), 3.53f, 2},
    // Stier, Plejaden, Hyaden
    {"Aldebaran", 4, 35.92, D(16, 30.5), 0.87f, 3}, {"Elnath", 5, 26.29, D(28, 36.4), 1.65f, 0},
    {"ZetaTau", 5, 37.65, D(21, 8.5), 2.97f, 0}, {"Alcyone", 3, 47.48, D(24, 6.3), 2.87f, 0},
    {"Atlas", 3, 49.16, D(24, 3), 3.62f, 0}, {"Electra", 3, 44.88, D(24, 6.8), 3.70f, 0},
    {"Maia", 3, 45.83, D(24, 22), 3.87f, 0}, {"Merope", 3, 46.33, D(23, 56.9), 4.18f, 0},
    {"Taygeta", 3, 45.21, D(24, 28), 4.30f, 0}, {"Theta2Tau", 4, 28.66, D(15, 52), 3.40f, 1},
    {"GammaTau", 4, 19.79, D(15, 37.7), 3.65f, 3}, {"Delta1Tau", 4, 22.93, D(17, 32.5), 3.76f, 3},
    {"EpsTau", 4, 28.62, D(19, 10.8), 3.53f, 3},
    // Fuhrmann
    {"Capella", 5, 16.69, D(45, 59.9), 0.08f, 2}, {"Menkalinan", 5, 59.53, D(44, 56.8), 1.90f, 1},
    {"ThetaAur", 5, 59.72, D(37, 12.7), 2.62f, 1}, {"Hassaleh", 4, 56.99, D(33, 9.9), 2.69f, 3},
    {"EpsAur", 5, 1.97, D(43, 49), 3.00f, 2},
    // Löwe
    {"Regulus", 10, 8.37, D(11, 58), 1.36f, 0}, {"Denebola", 11, 49.06, D(14, 34.3), 2.11f, 1},
    {"Algieba", 10, 19.97, D(19, 50.5), 2.08f, 3}, {"Zosma", 11, 14.11, D(20, 31.4), 2.56f, 1},
    {"Chertan", 11, 14.24, D(15, 25.8), 3.33f, 1}, {"Adhafera", 10, 16.69, D(23, 25), 3.44f, 2},
    {"EpsLeo", 9, 45.85, D(23, 46), 2.98f, 2}, {"Rasalas", 9, 52.76, D(26, 0), 3.88f, 3},
    {"EtaLeo", 10, 7.33, D(16, 45.8), 3.49f, 1},
    // Bärenhüter
    {"Arcturus", 14, 15.66, D(19, 10.9), -0.05f, 3}, {"Izar", 14, 44.99, D(27, 4.5), 2.37f, 3},
    {"Muphrid", 13, 54.68, D(18, 23.9), 2.68f, 2}, {"Seginus", 14, 32.08, D(38, 18.5), 3.03f, 1},
    {"Nekkar", 15, 1.95, D(40, 23.4), 3.49f, 2}, {"DeltaBoo", 15, 15.50, D(33, 18.9), 3.47f, 2},
    {"RhoBoo", 14, 31.83, D(30, 22), 3.58f, 3},
    // Jungfrau
    {"Spica", 13, 25.19, D(-11, 9.7), 0.98f, 0}, {"Porrima", 12, 41.66, D(-1, 27), 2.74f, 1},
    {"Vindemiatrix", 13, 2.18, D(10, 57.5), 2.83f, 2}, {"Minelauva", 12, 55.60, D(3, 23.8), 3.38f, 4},
    {"Zavijava", 11, 50.70, D(1, 45.9), 3.61f, 2}, {"ZetaVir", 13, 34.69, -0.597, 3.37f, 1},
    // Nördliche Krone
    {"Alphecca", 15, 34.69, D(26, 42.9), 2.23f, 1}, {"Nusakan", 15, 27.83, D(29, 6), 3.68f, 1},
    {"ThetaCrB", 15, 32.93, D(31, 21.5), 4.14f, 0}, {"GammaCrB", 15, 42.74, D(26, 17.7), 3.84f, 1},
    {"DeltaCrB", 15, 49.59, D(26, 4), 4.63f, 2}, {"EpsCrB", 15, 57.59, D(26, 52.7), 4.15f, 3},
    // Leier
    {"Vega", 18, 36.94, D(38, 47), 0.03f, 0}, {"Sheliak", 18, 50.08, D(33, 21.8), 3.52f, 0},
    {"Sulafat", 18, 58.94, D(32, 41.4), 3.25f, 0}, {"Zeta1Lyr", 18, 44.77, D(37, 36.3), 4.36f, 1},
    {"Delta2Lyr", 18, 54.51, D(36, 53.9), 4.30f, 4}, {"EpsLyr", 18, 44.33, D(39, 40), 4.67f, 1},
    // Schwan
    {"Deneb", 20, 41.43, D(45, 16.8), 1.25f, 1}, {"Sadr", 20, 22.23, D(40, 15.4), 2.23f, 2},
    {"GienahCyg", 20, 46.21, D(33, 58), 2.48f, 3}, {"DeltaCyg", 19, 44.98, D(45, 7.8), 2.87f, 0},
    {"Albireo", 19, 30.72, D(27, 57.6), 3.08f, 3}, {"EtaCyg", 19, 56.31, D(35, 5), 3.89f, 3},
    {"ZetaCyg", 21, 12.93, D(30, 13.6), 3.21f, 2},
    // Adler
    {"Altair", 19, 50.78, D(8, 52), 0.76f, 1}, {"Tarazed", 19, 46.26, D(10, 36.8), 2.72f, 3},
    {"Alshain", 19, 55.31, D(6, 24.4), 3.71f, 2}, {"ZetaAql", 19, 5.41, D(13, 51.8), 2.99f, 1},
    {"DeltaAql", 19, 25.50, D(3, 6.9), 3.36f, 2}, {"LambdaAql", 19, 6.25, D(-4, 52.9), 3.43f, 0},
    {"ThetaAql", 20, 11.31, -0.817, 3.24f, 0},
    // Skorpion
    {"Antares", 16, 29.41, D(-26, 25.9), 1.06f, 4}, {"Shaula", 17, 33.61, D(-37, 6), 1.62f, 0},
    {"Sargas", 17, 37.32, D(-42, 59.9), 1.86f, 2}, {"Dschubba", 16, 0.33, D(-22, 37.3), 2.29f, 0},
    {"Acrab", 16, 5.44, D(-19, 48.3), 2.62f, 0}, {"PiSco", 15, 58.85, D(-26, 6.8), 2.89f, 0},
    {"TauSco", 16, 35.88, D(-28, 13), 2.82f, 0}, {"SigmaSco", 16, 21.19, D(-25, 35.6), 2.89f, 0},
    {"EpsSco", 16, 50.16, D(-34, 17.6), 2.29f, 3}, {"Mu1Sco", 16, 51.87, D(-38, 2.8), 3.04f, 0},
    {"Zeta2Sco", 16, 54.58, D(-42, 21.7), 3.62f, 3}, {"EtaSco", 17, 12.15, D(-43, 14), 3.33f, 1},
    {"Iota1Sco", 17, 47.58, D(-40, 7.6), 2.99f, 2}, {"KappaSco", 17, 42.49, D(-39, 1.8), 2.39f, 0},
    {"Lesath", 17, 30.76, D(-37, 17.7), 2.70f, 0},
    // Schütze (Teekanne)
    {"KausAustralis", 18, 24.17, D(-34, 23), 1.85f, 0}, {"Nunki", 18, 55.27, D(-26, 17.8), 2.05f, 0},
    {"Ascella", 19, 2.61, D(-29, 52.8), 2.60f, 1}, {"KausMedia", 18, 20.99, D(-29, 49.7), 2.70f, 3},
    {"KausBorealis", 18, 27.97, D(-25, 25.3), 2.81f, 3}, {"Alnasl", 18, 5.81, D(-30, 25.4), 2.98f, 3},
    {"PhiSgr", 18, 45.66, D(-26, 59), 3.17f, 0}, {"TauSgr", 19, 6.94, D(-27, 40), 3.32f, 3},
    // Pegasus, Andromeda
    {"Markab", 23, 4.76, D(15, 12.3), 2.48f, 0}, {"Scheat", 23, 3.78, D(28, 4.9), 2.42f, 4},
    {"Algenib", 0, 13.24, D(15, 11), 2.83f, 0}, {"Alpheratz", 0, 8.39, D(29, 5.4), 2.06f, 0},
    {"Enif", 21, 44.19, D(9, 52.5), 2.39f, 3}, {"Homam", 22, 41.46, D(10, 49.9), 3.40f, 0},
    {"Matar", 22, 43.00, D(30, 13.3), 2.94f, 2}, {"Mirach", 1, 9.73, D(35, 37.2), 2.07f, 4},
    {"Almach", 2, 3.90, D(42, 19.8), 2.10f, 3}, {"DeltaAnd", 0, 39.33, D(30, 51.7), 3.27f, 3},
    // Perseus
    {"Mirfak", 3, 24.32, D(49, 51.7), 1.79f, 2}, {"Algol", 3, 8.17, D(40, 57.3), 2.12f, 0},
    {"ZetaPer", 3, 54.13, D(31, 53), 2.85f, 0}, {"EpsPer", 3, 57.85, D(40, 0.6), 2.89f, 0},
    {"GammaPer", 3, 4.80, D(53, 30.4), 2.93f, 2}, {"DeltaPer", 3, 42.93, D(47, 47.3), 3.01f, 0},
    // Drache
    {"Eltanin", 17, 56.61, D(51, 29.3), 2.24f, 3}, {"Rastaban", 17, 30.43, D(52, 18), 2.79f, 2},
    {"Thuban", 14, 4.39, D(64, 22.6), 3.65f, 1}, {"Aldibain", 16, 23.99, D(61, 30.8), 2.74f, 2},
    {"Altais", 19, 12.56, D(67, 39.7), 3.07f, 2}, {"Grumium", 17, 53.53, D(56, 52.4), 3.75f, 3},
    {"ZetaDra", 17, 8.79, D(65, 42.9), 3.17f, 0}, {"Edasich", 15, 24.93, D(58, 57.9), 3.29f, 3},
    {"KappaDra", 12, 33.48, D(69, 47.3), 3.87f, 0}, {"LambdaDra", 11, 31.40, D(69, 19.9), 3.84f, 4},
    // Herkules, Schlangenträger
    {"ZetaHer", 16, 41.29, D(31, 36.2), 2.81f, 2}, {"EtaHer", 16, 42.90, D(38, 55.3), 3.48f, 2},
    {"PiHer", 17, 15.05, D(36, 48.6), 3.16f, 3}, {"EpsHer", 17, 0.29, D(30, 55.6), 3.92f, 1},
    {"Kornephoros", 16, 30.22, D(21, 29.4), 2.77f, 2}, {"DeltaHer", 17, 15.03, D(24, 50.4), 3.14f, 1},
    {"Rasalgethi", 17, 14.65, D(14, 23.4), 3.37f, 4}, {"Rasalhague", 17, 34.94, D(12, 33.6), 2.08f, 1},
    // Widder, Walfisch, Südlicher Fisch, Wasserschlange, Rabe, Waage
    {"Hamal", 2, 7.17, D(23, 27.7), 2.00f, 3}, {"Sheratan", 1, 54.64, D(20, 48.5), 2.64f, 1},
    {"Mesarthim", 1, 53.53, D(19, 17.6), 3.86f, 0}, {"Menkar", 3, 2.28, D(4, 5.4), 2.54f, 4},
    {"Diphda", 0, 43.59, D(-17, 59.2), 2.04f, 3}, {"Fomalhaut", 22, 57.65, D(-29, 37.3), 1.16f, 1},
    {"Alphard", 9, 27.59, D(-8, 39.5), 1.98f, 3}, {"GienahCrv", 12, 15.81, D(-17, 32.5), 2.59f, 0},
    {"Kraz", 12, 34.39, D(-23, 23.8), 2.65f, 2}, {"Algorab", 12, 29.87, D(-16, 30.9), 2.95f, 1},
    {"Minkar", 12, 10.13, D(-22, 37), 3.00f, 3}, {"Zubeneschamali", 15, 17.01, D(-9, 23), 2.61f, 0},
    {"Zubenelgenubi", 14, 50.88, D(-16, 2.5), 2.75f, 1},
};

// Sternbildlinien (Namen aus kStars)
const char* const kLines[][2] = {
    {"Dubhe", "Merak"}, {"Merak", "Phecda"}, {"Phecda", "Megrez"}, {"Megrez", "Dubhe"}, {"Megrez", "Alioth"},
    {"Alioth", "Mizar"}, {"Mizar", "Alkaid"},
    {"Polaris", "Yildun"}, {"Yildun", "EpsUMi"}, {"EpsUMi", "ZetaUMi"}, {"ZetaUMi", "Kochab"}, {"Kochab", "Pherkad"},
    {"Pherkad", "EtaUMi"}, {"EtaUMi", "ZetaUMi"},
    {"Caph", "Schedar"}, {"Schedar", "GammaCas"}, {"GammaCas", "Ruchbah"}, {"Ruchbah", "Segin"},
    {"Betelgeuse", "Meissa"}, {"Meissa", "Bellatrix"}, {"Betelgeuse", "Alnitak"}, {"Bellatrix", "Mintaka"},
    {"Alnitak", "Alnilam"}, {"Alnilam", "Mintaka"}, {"Alnitak", "Saiph"}, {"Mintaka", "Rigel"},
    {"Mirzam", "Sirius"}, {"Sirius", "Wezen"}, {"Wezen", "Adhara"}, {"Wezen", "Aludra"}, {"Procyon", "Gomeisa"},
    {"Castor", "Pollux"}, {"Castor", "Mebsuta"}, {"Mebsuta", "Tejat"}, {"Pollux", "Wasat"}, {"Wasat", "Alhena"},
    {"Aldebaran", "Theta2Tau"}, {"Theta2Tau", "GammaTau"}, {"GammaTau", "Delta1Tau"}, {"Delta1Tau", "EpsTau"},
    {"EpsTau", "Elnath"}, {"Aldebaran", "ZetaTau"},
    {"Capella", "Menkalinan"}, {"Menkalinan", "ThetaAur"}, {"ThetaAur", "Elnath"}, {"Elnath", "Hassaleh"},
    {"Hassaleh", "EpsAur"}, {"EpsAur", "Capella"},
    {"Regulus", "EtaLeo"}, {"EtaLeo", "Algieba"}, {"Algieba", "Adhafera"}, {"Adhafera", "Rasalas"}, {"Rasalas", "EpsLeo"},
    {"Algieba", "Zosma"}, {"Zosma", "Denebola"}, {"Denebola", "Chertan"}, {"Chertan", "Regulus"},
    {"Arcturus", "Izar"}, {"Izar", "DeltaBoo"}, {"DeltaBoo", "Nekkar"}, {"Nekkar", "Seginus"}, {"Seginus", "RhoBoo"},
    {"RhoBoo", "Arcturus"}, {"Arcturus", "Muphrid"},
    {"Zavijava", "Porrima"}, {"Porrima", "Minelauva"}, {"Minelauva", "Vindemiatrix"}, {"Porrima", "Spica"},
    {"Spica", "ZetaVir"}, {"ZetaVir", "Minelauva"},
    {"ThetaCrB", "Nusakan"}, {"Nusakan", "Alphecca"}, {"Alphecca", "GammaCrB"}, {"GammaCrB", "DeltaCrB"},
    {"DeltaCrB", "EpsCrB"},
    {"Vega", "EpsLyr"}, {"Vega", "Zeta1Lyr"}, {"Zeta1Lyr", "Sheliak"}, {"Sheliak", "Sulafat"}, {"Sulafat", "Delta2Lyr"},
    {"Delta2Lyr", "Zeta1Lyr"},
    {"Deneb", "Sadr"}, {"Sadr", "EtaCyg"}, {"EtaCyg", "Albireo"}, {"DeltaCyg", "Sadr"}, {"Sadr", "GienahCyg"},
    {"GienahCyg", "ZetaCyg"},
    {"Tarazed", "Altair"}, {"Altair", "Alshain"}, {"Tarazed", "ZetaAql"}, {"Altair", "DeltaAql"},
    {"DeltaAql", "LambdaAql"}, {"Alshain", "ThetaAql"},
    {"Acrab", "Dschubba"}, {"Dschubba", "PiSco"}, {"Dschubba", "SigmaSco"}, {"SigmaSco", "Antares"},
    {"Antares", "TauSco"}, {"TauSco", "EpsSco"}, {"EpsSco", "Mu1Sco"}, {"Mu1Sco", "Zeta2Sco"}, {"Zeta2Sco", "EtaSco"},
    {"EtaSco", "Sargas"}, {"Sargas", "Iota1Sco"}, {"Iota1Sco", "KappaSco"}, {"KappaSco", "Shaula"}, {"Shaula", "Lesath"},
    {"KausAustralis", "KausMedia"}, {"KausMedia", "KausBorealis"}, {"KausMedia", "Alnasl"}, {"Alnasl", "KausAustralis"},
    {"KausAustralis", "Ascella"}, {"Ascella", "PhiSgr"}, {"PhiSgr", "KausMedia"}, {"PhiSgr", "KausBorealis"},
    {"PhiSgr", "Nunki"}, {"Nunki", "TauSgr"}, {"TauSgr", "Ascella"},
    {"Markab", "Scheat"}, {"Scheat", "Alpheratz"}, {"Alpheratz", "Algenib"}, {"Algenib", "Markab"}, {"Markab", "Homam"},
    {"Homam", "Enif"}, {"Scheat", "Matar"}, {"Alpheratz", "DeltaAnd"}, {"DeltaAnd", "Mirach"}, {"Mirach", "Almach"},
    {"Mirfak", "GammaPer"}, {"Mirfak", "DeltaPer"}, {"DeltaPer", "EpsPer"}, {"EpsPer", "ZetaPer"}, {"Mirfak", "Algol"},
    {"Eltanin", "Rastaban"}, {"Rastaban", "Grumium"}, {"Grumium", "Eltanin"}, {"Grumium", "Altais"},
    {"Altais", "ZetaDra"}, {"ZetaDra", "Aldibain"}, {"Aldibain", "Edasich"}, {"Edasich", "Thuban"},
    {"Thuban", "KappaDra"}, {"KappaDra", "LambdaDra"},
    {"ZetaHer", "EtaHer"}, {"EtaHer", "PiHer"}, {"PiHer", "EpsHer"}, {"EpsHer", "ZetaHer"}, {"ZetaHer", "Kornephoros"},
    {"EpsHer", "DeltaHer"}, {"DeltaHer", "Rasalgethi"},
    {"Hamal", "Sheratan"}, {"Sheratan", "Mesarthim"},
    {"GienahCrv", "Algorab"}, {"Algorab", "Kraz"}, {"Kraz", "Minkar"}, {"Minkar", "GienahCrv"},
    {"Zubeneschamali", "Zubenelgenubi"},
};

glm::dvec3 equatorial(double ra, double dec) {  // Bogenmaß
    return {std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec)};
}

// Himmelsäquator -> Ort (x Osten, y oben, z Süden) bei dieser Sternzeit (Bogenmaß)
glm::dmat3 localFrame(double lst) {
    const double phi = kLatitude * kDeg, s = std::sin(lst), c = std::cos(lst);
    glm::dmat3 m;
    m[0] = {-s, std::cos(phi) * c, std::sin(phi) * c};
    m[1] = {c, std::cos(phi) * s, std::sin(phi) * s};
    m[2] = {0.0, std::sin(phi), -std::cos(phi)};
    return m;
}

void sunEquatorial(double& ra, double& dec) {
    const double l = kSunLongitude * kDeg, e = kObliquity * kDeg;
    ra = std::atan2(std::cos(e) * std::sin(l), std::cos(l));
    dec = std::asin(std::sin(e) * std::sin(l));
}

// Luftmasse in Richtung dieser Höhe (Kasten & Young): 1 im Zenit, ~38 am Horizont
double airMass(double altDeg) {
    const double h = std::max(altDeg, -1.0);
    return 1.0 / (std::sin(h * kDeg) + 0.50572 * std::pow(h + 6.07995, -1.6364));
}

double smooth(double a, double b, double x) {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Himmel nach der Höhe der Sonne (Grad): Stützstellen, dazwischen linear
struct SkyKey {
    double alt;
    glm::vec3 zenith, horizon, glow, ambient, ground;
};
const SkyKey kKeys[] = {
    {-16, {0.0040f, 0.0070f, 0.0180f}, {0.014f, 0.019f, 0.032f}, {0.0f, 0.0f, 0.0f}, {0.016f, 0.021f, 0.036f}, {0.006f, 0.006f, 0.008f}},
    {-9, {0.010f, 0.016f, 0.045f}, {0.035f, 0.040f, 0.065f}, {0.06f, 0.02f, 0.01f}, {0.022f, 0.028f, 0.045f}, {0.010f, 0.010f, 0.011f}},
    {-4, {0.035f, 0.060f, 0.160f}, {0.22f, 0.15f, 0.16f}, {0.45f, 0.14f, 0.05f}, {0.060f, 0.065f, 0.10f}, {0.030f, 0.028f, 0.026f}},
    {0, {0.08f, 0.15f, 0.36f}, {0.62f, 0.42f, 0.30f}, {0.90f, 0.38f, 0.10f}, {0.17f, 0.18f, 0.25f}, {0.08f, 0.07f, 0.06f}},
    {4, {0.13f, 0.27f, 0.58f}, {0.70f, 0.66f, 0.66f}, {0.55f, 0.30f, 0.10f}, {0.30f, 0.34f, 0.45f}, {0.15f, 0.13f, 0.11f}},
    {12, {0.16f, 0.34f, 0.72f}, {0.62f, 0.72f, 0.86f}, {0.0f, 0.0f, 0.0f}, {0.36f, 0.45f, 0.62f}, {0.20f, 0.18f, 0.16f}},
};

}  // namespace

State at(double hours) {
    State st;
    double sunRa, sunDec;
    sunEquatorial(sunRa, sunDec);
    const double lst = (hours - 12.0) * 15.0 * kDeg + sunRa;  // Sternzeit: Stundenwinkel der Sonne + ihre Rektaszension
    const glm::dmat3 M = localFrame(lst);
    const glm::dvec3 sun = M * equatorial(sunRa, sunDec);
    const glm::dvec3 moon = M * equatorial(sunRa + kMoonBehind * kDeg, kMoonDec * kDeg);
    st.sunDir = glm::vec3(sun);
    st.moonDir = glm::vec3(moon);
    st.sunAlt = std::asin(std::clamp(sun.y, -1.0, 1.0)) / kDeg;
    st.moonAlt = std::asin(std::clamp(moon.y, -1.0, 1.0)) / kDeg;
    st.starRot = glm::mat4(glm::mat3(M));

    // Himmel und Umgebungslicht nach der Sonnenhöhe
    const int nk = (int)(sizeof(kKeys) / sizeof(kKeys[0]));
    const double a = std::clamp(st.sunAlt, kKeys[0].alt, kKeys[nk - 1].alt);
    int k = 0;
    while (k < nk - 2 && a > kKeys[k + 1].alt) ++k;
    const float t = (float)((a - kKeys[k].alt) / (kKeys[k + 1].alt - kKeys[k].alt));
    const SkyKey &k0 = kKeys[k], &k1 = kKeys[k + 1];
    st.zenith = glm::mix(k0.zenith, k1.zenith, t);
    st.horizon = glm::mix(k0.horizon, k1.horizon, t);
    st.glow = glm::mix(k0.glow, k1.glow, t);
    st.skyAmbient = glm::mix(k0.ambient, k1.ambient, t);
    st.groundAmbient = glm::mix(k0.ground, k1.ground, t);

    // Sonne: durch die Luft gerötet (blau wird zuerst gestreut), unter dem Horizont aus
    const double m = airMass(st.sunAlt);
    const glm::dvec3 tr = glm::exp(-glm::dvec3(0.025, 0.07, 0.17) * std::max(m - 1.0, 0.0));
    const double sunUp = smooth(-1.0, 3.0, st.sunAlt);
    const glm::vec3 sunLight = glm::vec3(glm::dvec3(3.4, 3.15, 2.9) * tr * sunUp);
    st.sunDisc = glm::vec3(glm::dvec3(1.0, 0.9, 0.75) * tr * smooth(-1.5, 0.5, st.sunAlt));

    // Mond: fast voll (Anteil der beleuchteten Scheibe aus dem Winkel zur Sonne). Sein Licht zählt erst, wenn die Dämmerung
    // vorbei ist; der Himmel um ihn wird etwas heller
    const double phase = 0.5 * (1.0 - glm::dot(sun, moon));
    const double moonUp = smooth(-1.0, 6.0, st.moonAlt) * phase;
    const double dark = 1.0 - smooth(-6.0, -1.0, st.sunAlt);
    st.moonBright = (float)(smooth(-1.0, 1.0, st.moonAlt) * phase);
    const float moonLight = (float)(moonUp * dark);
    st.skyAmbient += moonLight * glm::vec3(0.030f, 0.040f, 0.062f);
    st.zenith += moonLight * glm::vec3(0.0015f, 0.0028f, 0.0065f);
    st.horizon += moonLight * glm::vec3(0.0035f, 0.0055f, 0.0100f);

    // Licht und Schatten: die Sonne bis kurz unter den Horizont, danach der Mond (dort sind beide fast dunkel)
    if (st.sunAlt > -1.0) {
        st.lightDir = st.sunDir;
        st.lightColor = sunLight;
    } else {
        st.moonLight = true;
        st.lightDir = st.moonDir;
        st.lightColor = moonLight * glm::vec3(0.62f, 0.72f, 1.0f) * 0.45f;
    }

    st.stars = (float)(1.0 - smooth(-12.0, -3.0, st.sunAlt));
    // Das Auge gewöhnt sich an die Nacht: früher, ohne Lampen, sah man im Mondlicht noch gut
    st.exposure = (float)(2.6 - 1.6 * smooth(-10.0, 6.0, st.sunAlt));
    return st;
}

const std::vector<glm::vec4>& stars() {
    static const std::vector<glm::vec4> list = [] {
        std::vector<glm::vec4> v;
        for (const StarDef& s : kStars) {
            const double ra = (s.raH + s.raM / 60.0) * 15.0 * kDeg;
            v.push_back(glm::vec4(glm::vec3(equatorial(ra, s.dec * kDeg)), s.mag + 20.0f * (float)s.color));
            if ((int)v.size() == kMaxStars) break;
        }
        return v;
    }();
    return list;
}

const std::vector<glm::ivec2>& lines() {
    static const std::vector<glm::ivec2> list = [] {
        std::unordered_map<std::string, int> index;
        const int n = (int)stars().size();
        for (int i = 0; i < n; ++i) index[kStars[i].name] = i;
        std::vector<glm::ivec2> v;
        for (const auto& l : kLines) {
            auto a = index.find(l[0]), b = index.find(l[1]);
            if (a == index.end() || b == index.end()) continue;
            v.push_back({a->second, b->second});
            if ((int)v.size() == kMaxLines) break;
        }
        return v;
    }();
    return list;
}

std::string clock(double hours) {
    int minutes = (int)std::floor(std::fmod(std::fmod(hours, 24.0) + 24.0, 24.0) * 60.0 + 1e-6) % (24 * 60);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return buf;
}

bool parseTime(const std::string& s, double& hours) {
    double sunRa, sunDec;
    sunEquatorial(sunRa, sunDec);
    // halber Tagbogen der Sonne (Stunden): Sonnenaufgang 12 − h0, Untergang 12 + h0
    const double h0 = std::acos(std::clamp(-std::tan(kLatitude * kDeg) * std::tan(sunDec), -1.0, 1.0)) / kDeg / 15.0;
    static const std::unordered_map<std::string, double> fixed = {
        {"midnight", 0.0}, {"morning", 8.0}, {"day", 10.0}, {"noon", 12.0}, {"afternoon", 15.0}, {"night", 22.5},
    };
    if (auto it = fixed.find(s); it != fixed.end()) {
        hours = it->second;
        return true;
    }
    if (s == "sunrise") hours = 12.0 - h0;
    else if (s == "dawn") hours = 12.0 - h0 - 0.6;
    else if (s == "sunset" || s == "evening") hours = 12.0 + h0;
    else if (s == "dusk") hours = 12.0 + h0 + 0.6;
    else {
        char* end = nullptr;
        const double h = std::strtod(s.c_str(), &end);
        if (end == s.c_str()) return false;
        double min = 0;
        if (*end == ':') {
            const char* p = end + 1;
            min = std::strtod(p, &end);
            if (end == p || min < 0 || min >= 60) return false;
        }
        if (*end != '\0' || h < 0 || h > 24) return false;
        hours = std::fmod(h + min / 60.0, 24.0);
    }
    return true;
}

}  // namespace sky
