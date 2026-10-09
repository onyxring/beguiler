#!/usr/bin/env python3
"""Field-trial coverage: which parts of the language and the standard library the field trials exercise.

Each surface item is a spec section (or a standard-library feature) with a pattern that finds it in a
trial's source. An item no trial matches is a gap for the next round. Items a field trial can't reach
(the command line, the language server, debug builds, precompiler mode) are listed apart; other test
tiers cover them.

Patterns are deliberately simple: they find a feature used, not used well. Comments and strings are
stripped first, so a feature named only in prose doesn't count.

Usage: trial_coverage.py [--markdown] [--gaps] [--skip NAME,…]
"""
import argparse, glob, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
TRIALS = os.path.join(ROOT, 'tests', 'fieldTrials')

ID = r'[A-Za-z_]\w*'
# (spec section, item, pattern) — the pattern runs on comment- and string-stripped source, case-insensitively.
LANGUAGE = [
    ('1.6.2', 'float literals', r'\b\d+\.\d+\b'),
    ('1.6.4', 'raw string literals', r'@"'),
    ('1.6.5', 'interpolated strings', r'\$"'),
    ('1.6.6', 'character literals', r"'(\\.|[^'\\])'"),
    ('1.6.7', 'dictionary word literals', r"(?<![\w)\]])\.\.?[a-z]\w*\b(?!\s*\()"),
    ('2.3', 'float type', r'\bfloat\b'),
    ('2.5', 'null', r'\bnull\b'),
    ('2.6', 'var type', r'\bvar\s+' + ID),
    ('2.7.1', 'enum', r'\benum\s+' + ID),
    ('2.7.2', 'bnum', r'\bbnum\s+' + ID),
    ('2.7.4', 'extern enum / bnum', r'\bextern\s+(enum|bnum)\b'),
    ('2.8', 'union types', r'\b' + ID + r'(<[^>]*>)?\s*\|\s*' + ID + r'(<[^>]*>)?\s+' + ID + r'\s*[=;,)]'),
    ('2.8.1', 'typeof / eType', r'\btypeof\s*\('),
    ('2.8.2', 'named unions', r'\bunion\s+' + ID + r'\s*='),
    ('2.9', 'function types', r'\bfunc\s*<'),
    ('3.4', 'constants', r'\bconst\s+' + ID),
    ('3.7', 'ref and :=', r'\bref\s+' + ID + r'|:='),
    ('3.9', 'global qualifier ::', r'(?<![\w>])::' + ID),
    ('3.11', 'asI6 / asBgl', r'\bas(I6|Bgl)\b'),
    ('3.12', 'superposed', r'\bsuperposed\b'),
    ('4.6', 'three-way comparison <=>', r'<=>'),
    ('4.8', 'bitwise and shift operators', r'<<|>>|(?<![&|])[&|^](?![&|=])'),
    ('4.9', 'ternary', r'\?(?![.?])[^?;:]*:'),
    ('4.10', 'optional chaining / ??', r'\?\.|\?\?'),
    ('4.11', 'casts', r'(?:[=(,!+\-*/<>?:\[]|\breturn)\s*\(\s*' + ID + r'\s*\)\s*[\w(]'),
    ('4.11', 'receiver casts ((T)x).m', r'\(\(\s*' + ID + r'\s*\)\s*' + ID + r'\s*\)\s*\.'),
    ('4.12', 'address-of &', r'(?<![\w)\]&])&\s*' + ID),
    ('4.13', 'new (pooled classes)', r'\bnew\s+' + ID + r'\s*\('),
    ('4.14', 'lambdas', r'=>'),
    ('4.15', 'operator references', r'::\s*operator\b'),
    ('5.6', 'compound assignment', r'[-+*/%|&^]='),
    ('5.7', 'increment / decrement', r'\+\+|--'),
    ('5.9', 'C-style for', r'\bfor\s*\([^;)]*;'),
    ('5.9.1', 'for-in', r'\bfor\s*\([^;)]*\bin\b'),
    ('5.10', 'while', r'(?<!\})\s*\bwhile\s*\('),
    ('5.11', 'do / while, do / until', r'\bdo\s*\{'),
    ('5.12', 'switch', r'\bswitch\s*\('),
    ('5.13', 'break / continue', r'\b(break|continue)\s*;'),
    ('5.14', 'rtrue / rfalse', r'\br(true|false)\b'),
    ('5.15', 'delete', r'\bdelete\s+'),
    ('5.16', 'try / catch / throw', r'\btry\s*\{'),
    ('6.3', 'default parameter values', r'\(\s*[^)]*\b' + ID + r'\s+' + ID + r'\s*=\s*[^=)][^)]*\)\s*\{'),
    ('6.3', 'named arguments', r'\b' + ID + r'\s*\([^()]*\b' + ID + r'\s*:\s*[^:]'),
    ('6.4', 'overloads', None),   # computed: a routine name declared twice
    ('6.5', 'replace / replaced()', r'\breplaced?\b'),
    ('7.2', 'emitter functions', r'\bemitter\s+' + ID + r'[\w<>]*\s+' + ID + r'\s*\('),
    ('7.6', 'emitter values', r'\bemitter\s+' + ID + r'\s+' + ID + r'\s*\{'),
    ('7.3.4', '$i6Expr', r'\$i6Expr'),
    ('8.1.1', 'type parameters', r'\bextend\s+(extern\s+)?class\s+(array|rawArray)\b'),
    ('8.2.6', 'pooled classes', r'\bclass\s+' + ID + r'\s*\[\s*\d*\s*\]'),
    ('8.2.7', 'value classes', r'\bvalue\s+class\b'),
    ('8.3.2', 'const members', r'\bconst\s+\w+\s+\w+\s*=?[^(]*;'),
    ('8.3.3', 'static members', r'\bstatic\s+'),
    ('8.3.5', 'inline members', r'\binline\s+'),
    ('8.5', 'init / deinit', r'\b(init|deinit)\s*\('),
    ('8.6', 'inheritance', r'\bclass\s+' + ID + r'\s*(\[[^\]]*\])?\s*:\s*' + ID),
    ('8.6', 'multiple inheritance', r'\bclass\s+' + ID + r'\s*:\s*' + ID + r'\s*,\s*' + ID),
    ('8.7.1', 'extend class', r'\bextend\s+class\b'),
    ('8.7.3', 'default members', r'\bdefault\s+' + ID),
    ('8.7.4', 'hide', r'\bhide\s+'),
    ('9.1', 'operator overloads', r'\boperator\s*(\+|-|\*|/|%|==|!=|<|>|<=|>=|\[\]|\(\)|=)'),
    ('9.3', 'subscript operators', r'\boperator\s*\[\]'),
    ('9.4', 'conversion operator ()', r'\boperator\s*\(\)'),
    ('9.6', 'static operators', r'\bstatic\s+\w+\s+operator\b'),
    ('9.9', 'property accessors', r'\bauto\s+' + ID + r'\s*=\s*\{|\bauto\s*\{'),
    ('9.9.3', 'outer', r'\bouter\b'),
    ('10.1', 'namespace-scoped types', r'\b' + ID + r'(\.' + ID + r')+\s+' + ID + r'\s*[=;,)(]'),
    ('10.4', '#using', r'#using\b'),
    ('11.3', 'inline objects Type{ … }', r'\b' + ID + r'\s*\{\s*[^}]*\}\s*[;,)]'),
    ('11.5.1', 'parent', r'\.parent\b|\bparent\s*='),
    ('11.5.2', 'children', r'\.children\b'),
    ('11.5.3', 'attributes', r'\battributes\s*=|\.attributes\b'),
    ('11.6', 'attribute declarations', r'\battribute\s+' + ID + r'\s*;'),
    ('11.7.1', 'property declarations', r'\bproperty\s+' + ID + r'\s*;'),
    ('11.7.2', 'additive properties', r'\badditive\b'),
    ('11.7.3', 'computed property access', r'\.\(\s*' + ID + r'\s*\)|[(,]\s*property\s+' + ID + r'\s*[,)]'),
    ('11.10', 'extend object', r'\bextend\s+(?!class|enum|bnum|grammar)' + ID + r'\s*\{'),
    ('11.11', 'extern object', r'\bextern\s+object\b'),
    ('12.2', 'array<T>', r'\barray\s*<'),
    ('12.4', 'byte arrays array<char>', r'\barray\s*<\s*char\s*>'),
    ('12.6', 'local arrays', None),   # computed: an array declared inside a routine body
    ('12.7', 'member arrays', None),  # computed: an array declared as a class or object member
    ('12.8', 'rawArray<T>', r'\brawArray\s*<'),
    ('12.9', 'arrays of arrays', r'\barray\s*<\s*array\s*<'),
    ('12.11', 'extend array', r'\bextend\s+' + ID + r'\s*\{\s*(inject|remove|move)'),
    ('13.2', 'verb declarations', r'\bverb\s+' + ID + r'\s*(:\s*' + ID + r')?\s*\{|\bclass\s+\w+\s*:\s*verb\b'),
    ('13.2.1', 'handler()', r'\bhandler\s*\('),
    ('13.2.2', 'perform() / <action>', r'\bperform\s*\(|<\s*' + ID + r'[^<>;]*>\s*;'),
    ('13.2.4', 'meta verbs', r'\bmeta\s*='),
    ('13.3', 'action comparisons', r'\baction\s*==|==\s*##|\baction\s*!='),
    ('13.4', 'grammar', r'\bgrammar\b'),
    ('13.5.1', 'grammar +=', r'\bgrammar\s*\+='),
    ('13.5.4', 'synonyms', r'\bsynonyms\b'),
    ('14.1.2', '#include "file"', r'#include\s+"'),
    ('14.2.1', '#define', r'#define\b'),
    ('14.2.5', '#if', r'#if\b'),
    ('14.3', '#message / #warning', r'#(message|warning)\b'),
    ('14.4.1', '#startup / #emitfirst / #emitlast', r'#(startup|emitfirst|emitlast)\b'),
    ('14.5.1', '#i6 islands', r'#i6\b'),
    ('15.4', 'extern declarations', r'\bextern\s+(?!enum|bnum|object)'),
    ('15.6', 'replacing an I6 library routine', r'\breplace\s+extern\b|\breplace\s+\w+\s+\w+\s*\('),
    ('21.4', 'print / printLine', r'\bprint(Line)?\s*\('),
    ('21.4', 'article helpers', r'\b(a|cA|the|cThe)\s*\(|\{\s*(a|cA|the|cThe)\s*:'),
    ('21.5.1', 'attribute values (has / give)', r'\bhas\b|\.give\s*\(|\battributes\b'),
    ('21.5.3', 'dictionaryWord', r'\bdictionaryWord\b'),
    ('21.5.10', 'stringOrRoutine', r'\bstringOrRoutine\b'),
    ('21.6.1', 'uint', r'\buint\b'),
    ('21.6.2', 'bgl.util.math', r'\bmath\.' + ID),
    ('21.6.3', 'bgl.util.random', r'\brandom\.' + ID + r'|\brandom\s*\('),
    ('21.7', 'character utilities', r'\.(isUpper|isLower|isAlpha|isNumeric|toUpper|toLower|isVowel)\s*\('),
    ('21.9', 'bgl.world', r'\bworld\.' + ID),
    ('21.11', 'bgl.printRules', r'\bprintRules\b|\{\s*(bold|italics|underline|fixed|link|emphasized)\s*:'),
    ('21.13', 'bgl.asm', r'\basm\.' + ID),
    ('21.14', 'bgl.header', r'\bheader\.' + ID),
    ('22.2', '<buf>', r'#include\s*<buf>|\bbuf\b'),
    ('22.3', '<string> / stringObj', r'\bstringObj\b'),
    ('22.4', '<array> methods', r'\.(push|pop|insert|removeAt|indexOf|contains|sort|clear|fill|reverse)\s*\('),
    ('22.5', '<linq>', r'\.(where|select|orderBy|any|all|count|first|sum|filter|map)\s*\('),
    ('22.7', '<glulxWindow>', r'\bsplit(Up|Down|Left|Right)\w*\s*\(|\bmainWin\b'),
    ('22.7.7', 'window styles', r'\bstyleSheet\b|\bchildStyles\b'),
    ('22.7.12', 'capabilities', r'\bsupports\b'),
    ('22.8', '<glulxImage>', r'\beImages\b|\bglulxImage\b'),
]

LIBRARY = [
    ('rooms and map connections', r'\b[nsewud]_to\b|\b(ne|nw|se|sw|in|out)_to\b'),
    ('doors', r'\bdoor_to\b|\bdoor_dir\b|\bdoor\b'),
    ('lockable and keys', r'\blockable\b|\bwith_key\b'),
    ('containers', r'\bcontainer\b'),
    ('supporters', r'\bsupporter\b'),
    ('openable', r'\bopenable\b'),
    ('enterable', r'\benterable\b'),
    ('transparent', r'\btransparent\b'),
    ('capacity', r'\bcapacity\b'),
    ('clothing / worn', r'\bclothing\b|\bworn\b'),
    ('light and darkness', r'\blight\b'),
    ('scenery / static', r'\bscenery\b|\bstatic\b'),
    ('concealed', r'\bconcealed\b'),
    ('switchable devices', r'\bswitchable\b'),
    ('edible', r'\bedible\b'),
    ('animate NPCs and life', r'\banimate\b|\blife\b'),
    ('orders to NPCs', r'\borders\b'),
    ('talkable', r'\btalkable\b'),
    ('before / after', r'\bbefore\b|\bafter\b'),
    ('react_before / react_after', r'\breact_(before|after)\b'),
    ('each_turn', r'\beach_turn\b'),
    ('daemons', r'\bdaemon\b|StartDaemon|StopDaemon'),
    ('timers', r'\btime_left\b|\btime_out\b|StartTimer|StopTimer'),
    ('found_in (multi-room objects)', r'\bfound_in\b'),
    ('add_to_scope / scope', r'\badd_to_scope\b|\bscope\b|PlaceInScope'),
    ('parse_name', r'\bparse_name\b'),
    ('short_name / article / proper', r'\bshort_name\b|\barticle\b|\bproper\b'),
    ('plurals', r'\bplural(name)?\b|\bpluralname\b'),
    ('list_together', r'\blist_together\b'),
    ('invent / describe', r'\binvent\b|\bdescribe\b'),
    ('initial / when_open / when_on', r'\binitial\b|\bwhen_(open|closed|on|off)\b'),
    ('cant_go', r'\bcant_go\b'),
    ('scoring', r'\bscore\b|\bscored\b|\bMAX_SCORE\b'),
    ('status line', r'\bstatusBar\b|\bstatusline\b|DrawStatusLine'),
    ('custom grammar and verbs', r'\bgrammar\b|\bverb\b'),
    ('topics and consult', r'\bconsult_\w+|\btopic\b'),
    ('number tokens / parsed_number', r'\bnumber\b|\bparsed_number\b'),
    ('yes/no questions', r'\bYesOrNo\b'),
    ('parser hooks (ParserError, ChooseObjects…)', r'\bParserError\b|\bChooseObjects\b|\bBeforeParsing\b|\bext_\w+'),
    ('NextWord / wn', r'\bNextWord\w*\b|\bwn\b'),
    ('deadflag / ending', r'\bdeadflag\b'),
    ('player / location changes', r'\bPlayerTo\b|\bChangePlayer\b|\blocation\b'),
    ('objectloop / tree walks', r'\bchildren\b|\bparent\b|\bobjectloop\b'),
    ('move / remove at run time', r'\bmove\s*\(|\.move\s*\(|\bremove\s*\(|\.remove\s*\(|\bparent\s*='),
    ('vehicles (moving with the player inside)', r'\bvehicle\b|\bdrive\b'),
]

OUT_OF_REACH = [
    ('16', 'command line and options'), ('16.6', 'language-server mode'), ('15.1.2', 'precompiler mode (.inf entry)'),
    ('17.6', 'blorb packaging'), ('18', 'compilation model and emission order'), ('19', 'diagnostics'),
    ('20', 'outputs and debug builds'),
]


def strip(src):
    """Blank out comments and the contents of strings and character literals, keeping their delimiters
    (and a `$` or `@` prefix) so the patterns for literals still see them."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith('//', i):
            i = src.find('\n', i)
            i = n if i < 0 else i
        elif src.startswith('/*', i):
            i = src.find('*/', i + 2)
            i = n if i < 0 else i + 2
            out.append(' ')
        elif c == '"' or (c == "'" and i + 2 < n and (src[i + 1] == '\\' or src[i + 2] == "'")):
            # An interpolated string's `{ … }` holes are code: keep them, blank the text around them.
            holes, j = [], i + 1
            interpolated = c == '"' and i > 0 and src[i - 1] == '$'
            while j < n and src[j] != c:
                if interpolated and src[j] == '{' and not src.startswith('{{', j):
                    depth, k = 1, j + 1
                    while k < n and depth:
                        if src[k] == '"':
                            k = src.find('"', k + 1)
                            k = n if k < 0 else k
                        elif src[k] == '{':
                            depth += 1
                        elif src[k] == '}':
                            depth -= 1
                        k += 1
                    holes.append(strip(src[j + 1:k - 1]))
                    j = k
                    continue
                j += 2 if src[j] == '\\' else 1
            out.append(c + ('x' if c == "'" else '') + c + ''.join(' ; ' + h for h in holes))
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def computed(item, src):
    if item == 'overloads':
        names = re.findall(r'^\s*(?:static\s+)?' + ID + r'[\w<>|, ]*\s+(' + ID + r')\s*\([^;]*\)\s*\{', src, re.M)
        lower = [n.lower() for n in names if n.lower() not in ('if', 'while', 'for', 'switch')]
        return len(lower) != len(set(lower))
    if item == 'local arrays':
        return re.search(r'\)\s*\{[^}]*\barray\s*<[^>]*>+\s*' + ID + r'\s*(\[|=)', src) is not None
    if item == 'member arrays':
        return re.search(r'\b(class|object)\b[^{]*\{[^}]*\barray\s*<[^>]*>+\s*' + ID + r'\s*(\[|=|;)', src) is not None
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--markdown', action='store_true')
    ap.add_argument('--gaps', action='store_true', help='list only what no trial covers')
    ap.add_argument('--skip', default='', help='comma-separated trial names to leave out')
    a = ap.parse_args()
    skip = {s.strip() for s in a.skip.split(',') if s.strip()}
    trials = {}
    for path in sorted(glob.glob(os.path.join(TRIALS, '*.bgl')) + glob.glob(os.path.join(TRIALS, '*_parts', '*.bgl'))):
        name = os.path.basename(path)
        if name.startswith('_'):
            continue
        key = os.path.basename(os.path.dirname(path)).replace('_parts', '') if path.endswith('.bgl') and '_parts' in path \
            else name[:-4]
        if key in skip:
            continue
        trials[key] = trials.get(key, '') + strip(open(path, encoding='utf-8', errors='replace').read())

    def hits(pattern, item):
        return [t for t, src in trials.items()
                if (computed(item, src) if pattern is None else re.search(pattern, src, re.I))]

    def section(title, rows):
        covered = sum(1 for r in rows if r[-1])
        out = [f"\n## {title} — {covered} of {len(rows)} covered\n" if a.markdown else f"\n{title}: {covered}/{len(rows)}"]
        if a.markdown:
            out += ['| § | Item | Trials |', '|---|---|---|']
        for *head, ts in rows:
            if a.gaps and ts:
                continue
            shown = ', '.join(ts) if ts else '**none**' if a.markdown else '—'
            out.append('| ' + ' | '.join(head + [shown]) + ' |' if a.markdown else f"  {' '.join(head):45} {shown}")
        return out

    lang = [(sec, item, hits(p, item)) for sec, item, p in LANGUAGE]
    lib = [('', item, hits(p, item)) for item, p in LIBRARY]
    out = [f"# Field-trial coverage\n\n{len(trials)} trials." if a.markdown else f"{len(trials)} trials."]
    out += section('Language (spec sections)', lang)
    out += section('Standard library', [(item, ts) for _, item, ts in lib])
    out.append('\n## Out of a field trial\'s reach\n' if a.markdown else '\nOut of reach (other tiers cover these):')
    out += [f"- §{s} {t}" if a.markdown else f"  §{s} {t}" for s, t in OUT_OF_REACH]
    print('\n'.join(out))


if __name__ == '__main__':
    main()
