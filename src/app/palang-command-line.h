/* palang-command-line.h — the `palang` subcommands for the terminal.
 *
 * Inside the Palang terminal, `palang help`, `palang settings`,
 * `palang get NAME`, `palang set NAME VALUE` and `palang reset NAME` read
 * and change preferences. The application decides *when* arguments are a
 * subcommand (only in the Palang terminal, see palang-application.c); this
 * file runs them. Replies are collected as text, so the application can
 * send them back to the calling terminal, and tests can check them without
 * a display.
 */

#pragma once

#include "palang-settings.h"

G_BEGIN_DECLS

/* TRUE if @word is the name of a subcommand ("set", "help", …). Anything
 * else on the command line is a file to open. */
gboolean palang_command_line_is_subcommand (const char *word);

/* Runs the subcommand in @args (the words after "palang", NULL-terminated;
 * args[0] must be a subcommand). Normal output is appended to @output,
 * error messages to @errors.
 * Returns: the exit status for the caller: 0 on success, 1 on errors. */
int      palang_command_line_run           (PalangSettings     *settings,
                                            const char * const *args,
                                            GString            *output,
                                            GString            *errors);

G_END_DECLS
