#pragma once

namespace ben_gear::cli {

/// Run the BenGear command-line application.
///
/// This is the CLI composition boundary: argument parsing, top-level commands,
/// interactive chat, and one-shot request execution live here instead of in the
/// process entry point. Core/runtime services remain UI-independent and are
/// composed through this adapter.
int run_cli(int argc, char** argv);

}  // namespace ben_gear::cli
