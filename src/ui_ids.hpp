#pragma once

namespace dc {

enum Control {
    NavHome = 100, NavExplorer, NavBurn, NavImages, NavSettings,
    Status, ProgressBar, Cancel,
    HomeDriveLabel, HomeDrive, HomeDriveState, Refresh, ActionLabel,
    TileExplore, TileImage, TileData, TileCopy, TileBurn, TileHash,
    ExploreDrive, ExploreFolder, ExploreIso, ExploreUp, ExplorePath, ExploreFilterLabel, ExploreFilter,
    ExploreList, ExploreOpen, ExploreCopy, ExploreCatalog, ExploreCount,
    BurnerLabel, Burner, Analyze, Eject, BurnMedia, ImageLabel, VolumeName, FormatLabel, Format,
    SpeedLabel, Speed, Verify, BurnFolder, BurnIso,
    ImageCreate, ImageSaveDisc, ImageBrowse, ImageHash, HashExpectedLabel, HashExpected, HashLabel, Hash,
    PreferencesLanguage, LanguageLabel, PreferencesProfile, ProfileLabel, PreferencesTheme, ThemeLabel,
    PreferencesAccent, AccentLabel, PreferencesFont, FontLabel, PreferencesBuffer, BufferLabel,
    PreferencesThreshold, ThresholdLabel, PreferencesCompact, PreferencesVerify, Apply, OpenLog, Memory,
    LastControl
};

inline constexpr UINT job_finished_message = WM_APP + 2;

}
