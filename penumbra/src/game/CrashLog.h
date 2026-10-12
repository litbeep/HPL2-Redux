// GPLv3 or later.
#ifndef PENUMBRA_CRASH_LOG_H
#define PENUMBRA_CRASH_LOG_H

namespace penumbra {
	// Writes the stack of an unhandled crash (including a stack overflow) to
	// the log, with names from the PDB next to the executable. Windows only;
	// elsewhere it does nothing.
	void InstallCrashLog();
}

#endif
