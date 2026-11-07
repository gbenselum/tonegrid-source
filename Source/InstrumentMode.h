#pragma once
#include "Effects/Pedalboard/PedalDefinitions.h"
#include "Effects/GraphicEq/GraphicEqProcessor.h"
#include <array>

namespace tonegrid
{
enum class InstrumentMode : int
{
    Guitar = 0,
    Bass = 1
};

enum class BassRackOrder : int
{
    CompThenDrive = 0,
    DriveThenComp = 1
};

constexpr bool isBass(InstrumentMode mode) noexcept
{
    return mode == InstrumentMode::Bass;
}

// In Bass mode, all guitar overdrives/distortions (and single-band StudioComp,
// which is superseded by TRI-COMP) are removed from the pedalboard to make
// visual and signal-chain space for TRI-COMP and TRI-DRIVE before the amp.
constexpr bool isPedalRemovedInBassMode(int persistentId) noexcept
{
    using board::PedalId;
    using board::id;
    return persistentId == id(PedalId::StudioComp)
        || persistentId == id(PedalId::TrebleBoost)
        || persistentId == id(PedalId::TransparentOd)
        || persistentId == id(PedalId::HorseOd)
        || persistentId == id(PedalId::TubeScreamer)
        || persistentId == id(PedalId::BluesOd)
        || persistentId == id(PedalId::FilterDist)
        || persistentId == id(PedalId::OrangeDist);
}

constexpr bool isPedalActiveOnBoard(int persistentId, InstrumentMode mode) noexcept
{
    return !isBass(mode) || !isPedalRemovedInBassMode(persistentId);
}

// The 5 stompboxes kept on the lower row of the Bass board (alongside the two
// wide pre-amp multiband units TRI-COMP and TRI-DRIVE on the upper row).
inline constexpr std::array<int, 5> bassStompboxIds{{
    board::id(board::PedalId::AutoWah),
    board::id(board::PedalId::ClassicFuzz),
    board::id(board::PedalId::VintageFuzz),
    board::id(board::PedalId::GraphicEq),
    board::id(board::PedalId::CleanBoost)
}};

inline constexpr std::array<float, 7> bassEqFrequencies{{
    40.f, 80.f, 160.f, 320.f, 640.f, 1300.f, 2600.f
}};

inline constexpr std::array<board::EqPreset, 8> bassEqPresets{{
    {"01 Flat",           { 0,  0,  0,  0,  0,  0,  0}},
    {"02 Sub Weight",     { 4,  3,  0, -2,  0,  1,  0}},
    {"03 Finger Growl",   { 1,  2,  3,  4,  2, -1, -2}},
    {"04 Slap Scoop",     { 4,  3, -2, -5, -2,  3,  4}},
    {"05 Pick Attack",    { 1,  2, -1,  1,  4,  3,  1}},
    {"06 Vintage Motown", { 2,  4,  2,  0, -2, -4, -6}},
    {"07 Modern Clank",   { 3,  2, -3, -2,  3,  5,  3}},
    {"08 Tame Fret Buzz", { 2,  2,  1,  1,  0, -3, -6}}
}};

} // namespace tonegrid
