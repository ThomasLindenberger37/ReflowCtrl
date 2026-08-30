## Current Hardware

- Board: LC Technology ESP32 Relay AC X1
- MCU/module: ESP32-WROOM-32E
- ESP-IDF target: `esp32`
- Board power input: 90–250 V AC
- Onboard relay: GPIO16
- Onboard LED (`LedLink`): GPIO23
- MAX6675 SO / MISO: GPIO25   yellow
- MAX6675 SCK: GPIO32         orange
- MAX6675 CS: GPIO27          braun

Never connect or use the programming interface while the board is powered from mains voltage. Programming/debugging must only be performed while the board is disconnected from mains and powered through an appropriate low-voltage programming/debug setup.

## Source Language

- Use C++ source files (`.cpp`) and C++ header files (`.hpp`) for project code.


C++ File Conventions

Use C++ for project code unless there is a concrete reason to use C.

Use:

.cpp for C++ implementation files
.hpp for C++ header files

Do not introduce .cc, .cxx, .hxx, or .h for new C++ code.

Existing ESP-IDF C APIs may of course still be included and used from C++.

Formatting

All C and C++ source files must comply with the repository's clang-format configuration.

Whenever source code is added or modified:

ensure clang-format passes
format the affected files before considering the task complete
do not manually fight or bypass the configured formatting rules

If no .clang-format exists yet and a task requires formatting infrastructure, create a reasonable project-level configuration rather than relying on editor-specific defaults.

Static Analysis

All new or modified C++ code should pass clang-tidy.

Treat clang-tidy warnings as issues that should normally be fixed rather than ignored.

Do not suppress a warning unless:

the warning is genuinely incorrect or unavoidable,
the reason is clearly understood,
and the suppression is as narrow as possible.

Avoid broad project-wide suppressions merely to make checks pass.

Prefer fixing the design or implementation instead.

Logging

Use ESP-IDF logging through ESP_LOG* sparingly.

Logging should provide useful diagnostic information, not narrate normal program execution.

Good reasons to log include:

startup summaries
important state transitions
hardware or configuration failures
unexpected conditions
recoverable errors
information useful for diagnosing field problems

Avoid:

logging every function entry or exit
logging every control-loop iteration
repetitive messages during normal operation
verbose informational logs that provide no diagnostic value

Choose the appropriate log level deliberately.

Errors should contain enough context to identify what failed and why.

Clean Code

Clean Code is a core project requirement.

Prefer code that is:

simple
explicit
readable
easy to maintain
easy to test
unsurprising to another developer

Use clear names and small focused functions.

Keep responsibilities separated.

Avoid:

oversized classes
long functions
hidden side effects
unnecessary global state
magic constants
clever abstractions
duplicated logic
premature generalisation

Do not introduce design patterns or abstractions solely for architectural purity.

Prefer the simplest clean implementation that solves the current problem.

Portability

Keep application and domain logic as portable as reasonably practical.

Do not unnecessarily couple generic logic to:

ESP-IDF
FreeRTOS
GPIO
networking
persistent storage
specific ESP32 peripherals

Hardware- and platform-specific code should remain close to the infrastructure boundary.

Where practical, separate:

domain logic
control algorithms
profile handling
calculations
state machines

from ESP-IDF-specific implementation details.

Do not create interfaces for everything purely for portability. Introduce boundaries when they improve maintainability, testability, or make platform-specific dependencies explicit.

Code that does not need ESP-IDF should ideally be compilable as ordinary C++.

Completion Criteria

For tasks that modify C or C++ code, do not consider the implementation complete until:

the code builds successfully
clang-format has been applied or verified
relevant clang-tidy checks pass
obvious compiler warnings have been addressed

Do not weaken compiler, formatter, or static-analysis rules simply to make a change pass.
