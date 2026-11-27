#pragma once
namespace shim {
// The 2017 folder's Daytona.exe is TeknoParrot's patched file; the pristine
// one sits beside it as Daytona.exe.original. When loaded into the patched
// file, put the original bytes back in memory (the code edits and the
// rewritten script text in .rdata), so the game runs as the original. The
// files on disk are not touched. Returns true when the image now matches the
// original.
bool restore_original_image();
}  // namespace shim
