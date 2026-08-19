#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

TAG=${1:-${GITHUB_REF_NAME:-}}
INCLUDE_INTERNAL=${INCLUDE_INTERNAL:-}

if [ -n "$TAG" ] && git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    END=$TAG
else
    END=HEAD
fi

PREV=$(git describe --tags --abbrev=0 "$END^" 2>/dev/null || echo "")
RANGE=${PREV:+$PREV..}$END

added=()
changed=()
fixed=()
removed=()
security=()
skipped=()

lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }

capitalize() {
    first=$(printf '%s' "$1" | cut -c1 | tr '[:lower:]' '[:upper:]')
    printf '%s%s' "$first" "$(printf '%s' "$1" | cut -c2-)"
}

classify() {
    type=$1
    text=$2
    case "$type" in
    security) echo security ;;
    *)
        case "$(lower "$text")" in
        remove*|"drop "*|"delete "*) echo removed ;;
        *security*|*vulnerab*|*cve-*) echo security ;;
        *)
            case "$type" in
            feat) echo added ;;
            fix|hotfix|bugfix) echo fixed ;;
            docs|chore|ci|build|test|deps) echo skipped ;;
            *) echo changed ;;
            esac
            ;;
        esac
        ;;
    esac
}

while IFS=$'\x1f' read -r sha subject; do
    [ -n "$sha" ] || continue
    breaking=""
    if [[ $subject =~ ^([A-Za-z]+)(\([^\)]*\))?(!)?:[[:space:]]*(.*)$ ]]; then
        type=$(lower "${BASH_REMATCH[1]}")
        breaking=${BASH_REMATCH[3]}
        text=${BASH_REMATCH[4]}
    else
        type=""
        text=$subject
    fi
    [ -n "$text" ] || continue

    section=$(classify "$type" "$text")
    entry="- $(capitalize "$text") (\`$sha\`)"
    if [ -n "$breaking" ]; then
        entry="- **Breaking** — $(capitalize "$text") (\`$sha\`)"
        if [ "$section" = skipped ]; then section=changed; fi
    fi

    case "$section" in
    added) added+=("$entry") ;;
    fixed) fixed+=("$entry") ;;
    removed) removed+=("$entry") ;;
    security) security+=("$entry") ;;
    changed) changed+=("$entry") ;;
    skipped)
        if [ -n "$INCLUDE_INTERNAL" ]; then changed+=("$entry"); else skipped+=("$entry"); fi
        ;;
    esac
done < <(git log --no-merges --pretty=tformat:"%h%x1f%s" "$RANGE")

print_section() {
    title=$1
    shift
    [ "$#" -gt 0 ] || return 0
    printf '## %s\n\n' "$title"
    printf '%s\n' "$@" | awk '!seen[$0]++'
    printf '\n'
}

if [ -n "$PREV" ]; then
    printf '_Changes since %s._\n\n' "$PREV"
fi

print_section '✨ Added' ${added[@]+"${added[@]}"}
print_section '🔧 Changed' ${changed[@]+"${changed[@]}"}
print_section '🐛 Fixed' ${fixed[@]+"${fixed[@]}"}
print_section '🗑️ Removed' ${removed[@]+"${removed[@]}"}
print_section '🔒 Security' ${security[@]+"${security[@]}"}

total=$((${#added[@]} + ${#changed[@]} + ${#fixed[@]} + ${#removed[@]} + ${#security[@]}))
if [ "$total" -eq 0 ]; then
    printf '## 🔧 Changed\n\n'
    if [ "${#skipped[@]}" -gt 0 ]; then
        printf -- '- Maintenance release — %d internal commits, nothing user-facing.\n\n' "${#skipped[@]}"
    else
        printf -- '- Rebuild of the same code — no commits since the previous tag.\n\n'
    fi
fi
