#pragma once

// Stub for vendor OC_scales.h which #includes <FS.h> for SD-card scala
// file load/save. NT has no SD card; the File-taking methods
// (SaveToScala, LoadScala) are dead-code paths on this target. The stub
// satisfies the type reference at compile time; runtime calls would
// crash but are never made.
class File {};
class FS {};
