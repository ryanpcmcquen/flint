TITLE=flint

# This is a cross platform Makefile, it has been tested on Linux,
# macOS, and Windows (using make and nmake).
#
# IMPORTANT:
# - GNU/BSD make must NEVER parse Windows-only targets/prereqs (like source\*),
#   and nmake must NEVER be forced to interpret POSIX shell fragments (uname, case, backticks).
# - The reliable “one file” trick is:
#     * put the nmake/make selection in an nmake conditional
#     * hide that conditional from GNU/BSD make using the "# \" comment-continuation trick
#     * define ALL targets exactly once, and route platform differences through variables
#
# The section under '!IFDEF MAKEDIR' is for Windows (nmake), and the
# section under '!ELSE' is for everything else.

FLAGS=-Wall -Wextra -std=c99

# Shared prereq glob: keep it POSIX-style so GNU/BSD make never sees source\*
# (nmake is typically fine with forward slashes here).
DEPS=source/* source/*/*

# \
!IFDEF MAKEDIR # \
# -------------------- nmake (Windows) configuration -------------------- # \
# nmake uses cmd.exe semantics in recipes. Keep POSIX shell out of here. # \
CP=copy # \
MV=move # \
RM=del /q # \
CC=clang # \
# Windows paths/inputs for the compiler stage: # \
WIN_SOURCE=source\flint.c source\ai\*.c source\terminal\*.c source\util\*.c # \
WIN_TARGET=-o $(TITLE).exe # \
# Build commands (single-line; safe to call from shared targets): # \
RELEASE_CMD=$(CC) $(FLAGS) -O3 $(WIN_SOURCE) $(WIN_TARGET) # \
DEBUG_CMD=$(CC) $(FLAGS) -g $(WIN_SOURCE) $(WIN_TARGET) # \
MEMDEBUG_CMD=$(CC) $(FLAGS) -g -fsanitize=address $(WIN_SOURCE) $(WIN_TARGET) # \
TEST_CMD=$(CC) $(FLAGS) tests\test_flint.c source\flint.c source\ai\*.c source\terminal\*.c source\util\*.c -o test_flint.exe && test_flint.exe && $(RM) test_flint.exe # \
!ELSE
# -------------------- make (Linux/macOS/BSD) configuration --------------------
CP=cp -f
MV=mv -f
RM=rm -f
CC=clang

POSIX_SOURCE=source/flint.c source/ai/*.c source/terminal/*.c source/util/*.c

RELEASE_CMD=$(CC) $(FLAGS) -O3 $(POSIX_SOURCE) -o $(TITLE)
DEBUG_CMD=$(CC) $(FLAGS) -g $(POSIX_SOURCE) -o $(TITLE)
MEMDEBUG_CMD=$(CC) $(FLAGS) -g -fsanitize=address $(POSIX_SOURCE) -o $(TITLE)
TEST_CMD=$(CC) $(FLAGS) -DTEST_MODE tests/test_flint.c source/flint.c source/ai/*.c source/terminal/*.c source/util/*.c -o test_flint && ./test_flint && $(RM) test_flint

# \
!ENDIF

# -------------------- Shared targets (defined once) --------------------
# These targets must be parseable by both make and nmake.
# Platform differences are routed through the *_CMD variables above.

$(TITLE): $(DEPS)
	$(RELEASE_CMD)

# Windows will automatically overwrite the binary when using nmake,
# but we provide clean/force for people who like that kind of thing.
clean:
	$(RM) $(TITLE)
	$(RM) $(TITLE).exe

force: $(DEPS)
	$(RM) $(TITLE)
	$(RM) $(TITLE).exe
	$(RELEASE_CMD)

debug: $(DEPS)
	$(DEBUG_CMD)

memdebug: $(DEPS)
	$(MEMDEBUG_CMD)

test: $(DEPS)
	$(TEST_CMD)
