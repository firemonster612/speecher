#include "app/ShellCompletions.h"

#include <QMap>

#include <optional>

namespace speecher {
namespace {

enum class Paths { None, Files, Folders };

// What completes after an option, or as a command's plain arguments.
struct Values {
    QStringList words;
    // One of the kCompletion lists, read from speecher while completing.
    QString listed;
    Paths paths = Paths::None;
};

struct CompletedCommand {
    // "record start" for a subcommand, empty for speecher with no command.
    QString name;
    QStringList options;
    Values arguments;
};

// Every option --help documents, by name, with what follows it, or nothing
// for a flag.
QMap<QString, std::optional<Values>> completedOptions(const CompletionChoices &choices)
{
    const std::optional<Values> flag;
    return {
        {QStringLiteral("--format"), Values{{QStringLiteral("plain"), QStringLiteral("html")}}},
        {QStringLiteral("--profile"), Values{{}, kCompletionProfiles}},
        {QStringLiteral("--language"), Values{choices.spokenLanguages}},
        {QStringLiteral("--daemon"), flag},
        {QStringLiteral("--version"), flag},
        {QStringLiteral("--help"), flag},
        {QStringLiteral("--json"), flag},
        {QStringLiteral("--watch"), flag},
        {QStringLiteral("--headless"), flag},
        {QStringLiteral("--model"), Values{choices.speechProviders}},
        {QStringLiteral("--no-vocabulary"), flag},
        {QStringLiteral("--vocab-file"), Values{{}, {}, Paths::Files}},
        {QStringLiteral("--refine"), Values{choices.refinementProviders + QStringList{QStringLiteral("none")}}},
        {QStringLiteral("--cleanup"), Values{{}, kCompletionCleanupLevels}},
        {QStringLiteral("--tone"), Values{{}, kCompletionTones}},
        {QStringLiteral("--output"),
         Values{{QStringLiteral("beside"), QStringLiteral("none")}, {}, Paths::Folders}},
        {QStringLiteral("--stdout"), flag},
        {QStringLiteral("--raw"), flag},
        {QStringLiteral("--srt"), flag},
        {QStringLiteral("--vtt"), flag},
        // Its seconds are optional, so nothing is offered for them.
        {QStringLiteral("--until-silence"), flag},
        {QStringLiteral("--to"), Values{{}, {}, Paths::Files}},
        {QStringLiteral("--mic-only"), flag},
    };
}

// Every command --help documents, with its options and plain arguments.
// A command with subcommands, such as record, gets its entry from them.
QList<CompletedCommand> documentedCommands()
{
    const QStringList dictationChoices{QStringLiteral("--format"), QStringLiteral("--profile"),
                                       QStringLiteral("--language")};
    const QStringList sharedChoices{QStringLiteral("--model"),   QStringLiteral("--no-vocabulary"),
                                    QStringLiteral("--vocab-file"), QStringLiteral("--refine"),
                                    QStringLiteral("--cleanup"), QStringLiteral("--profile"),
                                    QStringLiteral("--tone"),    QStringLiteral("--language"),
                                    QStringLiteral("--raw"),     QStringLiteral("--json")};
    const Values files{{}, {}, Paths::Files};
    QList<CompletedCommand> commands{
        {{}, {QStringLiteral("--daemon"), QStringLiteral("--version"), QStringLiteral("--help")}, files},
        {QStringLiteral("toggle"), dictationChoices},
        {QStringLiteral("start"), dictationChoices},
        {QStringLiteral("stop")},
        {QStringLiteral("cancel")},
        {QStringLiteral("status"), {QStringLiteral("--json"), QStringLiteral("--watch")}},
        {QStringLiteral("last")},
        {QStringLiteral("vocabulary add")},
        {QStringLiteral("settings")},
        {QStringLiteral("setup")},
        {QStringLiteral("quit")},
        {QStringLiteral("transcribe"),
         sharedChoices
             + QStringList{QStringLiteral("--headless"), QStringLiteral("--output"), QStringLiteral("--stdout"),
                           QStringLiteral("--srt"), QStringLiteral("--vtt")},
         files},
        {QStringLiteral("listen"), sharedChoices + QStringList{QStringLiteral("--until-silence")}},
        {QStringLiteral("providers"), {QStringLiteral("--json")}},
        {QStringLiteral("record start"),
         {QStringLiteral("--to"), QStringLiteral("--mic-only"), QStringLiteral("--vocab-file")}},
        {QStringLiteral("record status"), {QStringLiteral("--json")}},
        {QStringLiteral("record stop")},
    };
    for (const QString &shell : kCompletionShells) {
        commands.append({QStringLiteral("completions ") + shell});
    }
    return commands;
}

// The documented commands, then one for each command with subcommands,
// whose arguments are those subcommands. speecher with no command also takes
// each command's first word.
QList<CompletedCommand> completedCommands()
{
    QList<CompletedCommand> commands = documentedCommands();
    QStringList firstWords;
    QMap<QString, QStringList> subcommands;
    for (const CompletedCommand &command : std::as_const(commands)) {
        const QStringList words = command.name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (!words.isEmpty() && !firstWords.contains(words.first())) {
            firstWords << words.first();
        }
        if (words.size() == 2) {
            subcommands[words.first()] << words.last();
        }
    }
    commands.first().arguments.words = firstWords;
    for (auto parent = subcommands.cbegin(); parent != subcommands.cend(); ++parent) {
        commands.append({parent.key(), {}, Values{parent.value()}});
    }
    return commands;
}

// The commands with subcommands, by name, as a shell's case pattern.
QString parentPattern(const QList<CompletedCommand> &commands)
{
    QStringList parents;
    for (const CompletedCommand &command : commands) {
        if (command.name.contains(QLatin1Char(' '))) {
            const QString parent = command.name.section(QLatin1Char(' '), 0, 0);
            if (!parents.contains(parent)) {
                parents << parent;
            }
        }
    }
    return parents.join(QLatin1Char('|'));
}

QString quoted(const QString &word)
{
    return QLatin1Char('\'') + word + QLatin1Char('\'');
}

QString zshValues(const Values &values)
{
    QStringList actions;
    if (!values.words.isEmpty()) {
        actions << QStringLiteral("compadd -- ") + values.words.join(QLatin1Char(' '));
    }
    if (!values.listed.isEmpty()) {
        actions << QStringLiteral("compadd -- ${(f)\"$(${~words[1]} completions --list %1 2>/dev/null)\"}")
                       .arg(values.listed);
    }
    if (values.paths == Paths::Files) {
        actions << QStringLiteral("_files");
    } else if (values.paths == Paths::Folders) {
        actions << QStringLiteral("_files -/");
    }
    return actions.isEmpty() ? QStringLiteral("return 1") : actions.join(QStringLiteral("; "));
}

QString zshScript(const QMap<QString, std::optional<Values>> &options, const QList<CompletedCommand> &commands)
{
    QStringList lines{
        QStringLiteral("#compdef speecher"),
        QStringLiteral("# speecher's completions for zsh, from `speecher completions zsh`."),
        {},
        QStringLiteral("_speecher() {"),
        QStringLiteral("  local command="),
        QStringLiteral("  (( CURRENT > 2 )) && command=${words[2]}"),
        QStringLiteral("  case $command in"),
        QStringLiteral("    (%1) (( CURRENT > 3 )) && command+=\" ${words[3]}\" ;;").arg(parentPattern(commands)),
        QStringLiteral("  esac"),
        QStringLiteral("  case ${words[CURRENT-1]} in"),
    };
    for (auto option = options.cbegin(); option != options.cend(); ++option) {
        if (option.value()) {
            lines << QStringLiteral("    (%1) %2; return ;;").arg(option.key(), zshValues(*option.value()));
        }
    }
    lines << QStringLiteral("  esac") << QStringLiteral("  local -a options")
          << QStringLiteral("  case $command in");
    for (const CompletedCommand &command : commands) {
        lines << QStringLiteral("    (%1) options=(%2) ;;")
                     .arg(quoted(command.name), command.options.join(QLatin1Char(' ')));
    }
    lines << QStringLiteral("  esac") << QStringLiteral("  if [[ $PREFIX == -* ]]; then")
          << QStringLiteral("    compadd -- $options") << QStringLiteral("    return") << QStringLiteral("  fi")
          << QStringLiteral("  case $command in");
    for (const CompletedCommand &command : commands) {
        // A command with no plain arguments offers its options instead.
        lines << QStringLiteral("    (%1) %2 ;;")
                     .arg(quoted(command.name),
                          command.arguments.words.isEmpty() && command.arguments.paths == Paths::None
                              ? QStringLiteral("compadd -- $options")
                              : zshValues(command.arguments));
    }
    lines << QStringLiteral("  esac") << QStringLiteral("}") << QString()
          // Loaded from $fpath, the file is the function; sourced, it registers it.
          << QStringLiteral("if [[ $zsh_eval_context[-1] == loadautofunc ]]; then")
          << QStringLiteral("  _speecher \"$@\"") << QStringLiteral("else")
          << QStringLiteral("  compdef _speecher speecher") << QStringLiteral("fi");
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString bashValues(const Values &values)
{
    QStringList actions;
    if (!values.words.isEmpty()) {
        actions << QStringLiteral("COMPREPLY+=($(compgen -W '%1' -- \"$cur\"))")
                       .arg(values.words.join(QLatin1Char(' ')));
    }
    // Not through compgen -W, which would expand a name from the settings
    // and run what is in it; each is escaped as the command line reads it.
    // The word typed may be escaped already, as a common prefix bash inserted.
    if (!values.listed.isEmpty()) {
        actions << QStringLiteral("while IFS= read -r name; do printf -v quoted %q \"$name\"; "
                                  "[[ $name == \"$cur\"* || $quoted == \"$cur\"* ]] && COMPREPLY+=(\"$quoted\"); "
                                  "done < <(\"${COMP_WORDS[0]}\" completions --list %1 2>/dev/null)")
                       .arg(values.listed);
    }
    // Read a line each, so a file name with a space stays one.
    if (values.paths != Paths::None) {
        actions << QStringLiteral("compopt -o filenames 2>/dev/null")
                << QStringLiteral("mapfile -t -O ${#COMPREPLY[@]} COMPREPLY < <(compgen -%1 -- \"$cur\")")
                       .arg(values.paths == Paths::Files ? QLatin1Char('f') : QLatin1Char('d'));
    }
    return actions.isEmpty() ? QStringLiteral(":") : actions.join(QStringLiteral("; "));
}

QString bashScript(const QMap<QString, std::optional<Values>> &options, const QList<CompletedCommand> &commands)
{
    QStringList lines{
        QStringLiteral("# speecher's completions for bash, from `speecher completions bash`."),
        {},
        QStringLiteral("_speecher() {"),
        QStringLiteral("  local cur=${COMP_WORDS[COMP_CWORD]} command= options= name quoted"),
        QStringLiteral("  COMPREPLY=()"),
        QStringLiteral("  (( COMP_CWORD > 1 )) && command=${COMP_WORDS[1]}"),
        QStringLiteral("  case $command in"),
        QStringLiteral("    %1) (( COMP_CWORD > 2 )) && command+=\" ${COMP_WORDS[2]}\" ;;")
            .arg(parentPattern(commands)),
        QStringLiteral("  esac"),
        QStringLiteral("  case ${COMP_WORDS[COMP_CWORD-1]} in"),
    };
    for (auto option = options.cbegin(); option != options.cend(); ++option) {
        if (option.value()) {
            lines << QStringLiteral("    %1) %2; return ;;").arg(option.key(), bashValues(*option.value()));
        }
    }
    lines << QStringLiteral("  esac") << QStringLiteral("  case $command in");
    for (const CompletedCommand &command : commands) {
        lines << QStringLiteral("    %1) options=%2 ;;")
                     .arg(quoted(command.name), quoted(command.options.join(QLatin1Char(' '))));
    }
    lines << QStringLiteral("  esac") << QStringLiteral("  if [[ $cur == -* ]]; then")
          << QStringLiteral("    COMPREPLY=($(compgen -W \"$options\" -- \"$cur\"))") << QStringLiteral("    return")
          << QStringLiteral("  fi") << QStringLiteral("  case $command in");
    for (const CompletedCommand &command : commands) {
        lines << QStringLiteral("    %1) %2 ;;")
                     .arg(quoted(command.name),
                          command.arguments.words.isEmpty() && command.arguments.paths == Paths::None
                              ? QStringLiteral("COMPREPLY=($(compgen -W \"$options\" -- \"$cur\"))")
                              : bashValues(command.arguments));
    }
    lines << QStringLiteral("  esac") << QStringLiteral("}") << QString()
          << QStringLiteral("complete -F _speecher speecher");
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// fish's -a arguments for values other than files.
QString fishArguments(const Values &values)
{
    QStringList arguments = values.words;
    if (!values.listed.isEmpty()) {
        arguments << QStringLiteral("(__speecher_list %1)").arg(values.listed);
    }
    if (values.paths == Paths::Folders) {
        arguments << QStringLiteral("(__fish_complete_directories)");
    }
    return quoted(arguments.join(QLatin1Char(' ')));
}

QString fishScript(const QMap<QString, std::optional<Values>> &options, const QList<CompletedCommand> &commands)
{
    QStringList lines{
        QStringLiteral("# speecher's completions for fish, from `speecher completions fish`."),
        {},
        QStringLiteral("function __speecher_command"),
        QStringLiteral("    set -l words (commandline -opc)"),
        QStringLiteral("    set -e words[1]"),
        QStringLiteral("    set -l command $words[1]"),
        QStringLiteral("    if contains -- \"$command\" %1; and set -q words[2]")
            .arg(parentPattern(commands).replace(QLatin1Char('|'), QLatin1Char(' '))),
        QStringLiteral("        set command \"$command $words[2]\""),
        QStringLiteral("    end"),
        QStringLiteral("    echo $command"),
        QStringLiteral("end"),
        {},
        QStringLiteral("function __speecher_is"),
        QStringLiteral("    set -l command (__speecher_command)"),
        QStringLiteral("    test \"$command\" = \"$argv\""),
        QStringLiteral("end"),
        {},
        QStringLiteral("function __speecher_list"),
        QStringLiteral("    set -l speecher (commandline -opc)[1]"),
        QStringLiteral("    $speecher completions --list $argv 2>/dev/null"),
        QStringLiteral("end"),
        {},
        QStringLiteral("complete -c speecher -f"),
    };
    for (const CompletedCommand &command : commands) {
        const QString test = QStringLiteral("__speecher_is %1").arg(command.name).trimmed();
        const QString condition = QStringLiteral("complete -c speecher -n '%1'").arg(test);
        const Values &arguments = command.arguments;
        if (!arguments.words.isEmpty() || !arguments.listed.isEmpty() || arguments.paths == Paths::Folders) {
            lines << condition + QStringLiteral(" -a ") + fishArguments(arguments);
        }
        if (arguments.paths == Paths::Files) {
            lines << condition + QStringLiteral(" -F");
        }
        for (const QString &name : command.options) {
            const std::optional<Values> &value = options.value(name);
            QString line = condition + QStringLiteral(" -l ") + name.mid(2);
            if (value && value->paths == Paths::Files) {
                line += QStringLiteral(" -r -F");
            } else if (value) {
                line += QStringLiteral(" -x -a ") + fishArguments(*value);
            }
            lines << line;
        }
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace

QString completionScript(const QString &shell, const CompletionChoices &choices)
{
    const QMap<QString, std::optional<Values>> options = completedOptions(choices);
    const QList<CompletedCommand> commands = completedCommands();
    if (shell == QStringLiteral("bash")) {
        return bashScript(options, commands);
    }
    if (shell == QStringLiteral("zsh")) {
        return zshScript(options, commands);
    }
    if (shell == QStringLiteral("fish")) {
        return fishScript(options, commands);
    }
    return {};
}

} // namespace speecher
