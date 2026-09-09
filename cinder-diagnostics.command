#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
export PYTHONUNBUFFERED=1
# Keep diagnostics artifacts and registry separate from the running application.
export FOUNDRY_HOME="$PWD/models/diagnostics-registry"
MODEL="$PWD/sha256-4d2396b16114669389d7555c15a1592aad584750310f648edad5ca8c4eccda17"
BUILD="$PWD/build/diagnostics"
FOUNDRY="$BUILD/foundry"
RUN_DIR="$PWD/benchmarks/runs/diagnostics-$(date +%Y%m%d-%H%M%S)-$$"
mkdir -p "$RUN_DIR"
export CINDER_DIAGNOSTICS_LOG="$RUN_DIR/diagnostics.log"
# tee keeps a complete on-disk log as well as live popup/terminal output.
exec > >(tee "$CINDER_DIAGNOSTICS_LOG") 2>&1
trap 'result=$?; printf "\nFAILED (exit %s) at line %s. Log: %s\n" "$result" "$LINENO" "$CINDER_DIAGNOSTICS_LOG"; exit "$result"' ERR
step() { printf '\n[%s/10] %s\n' "$1" "$2"; }
printf 'Cinder Foundry diagnostics\nResults: %s\n' "$RUN_DIR"

step 1 'Check development dependencies and initialize the backend'
if [[ "$(uname -s)" == Darwin ]]; then
    brew install cmake ninja gtk4 pkgconf python
fi
git submodule update --init --recursive

step 2 'Verify weights and metadata'
python3 scripts/verify_assets.py --manifest metadata/manifest.json \
    --weights-root . --metadata-root metadata

step 3 'Build the diagnostics copy of the CLI, GUI, and tests'
cmake --preset release -B "$BUILD" -DFOUNDRY_TEST_MODEL="$MODEL"
cmake --build "$BUILD" --parallel 2

step 4 'Run the complete CTest suite, including this model'
ctest --test-dir "$BUILD" --output-on-failure

step 5 'Import and verify the model in the diagnostics registry'
"$FOUNDRY" import ollama --manifest metadata/manifest.json \
    --weights-root . --metadata-root metadata --name smollm2

step 6 'Inspect, plan memory, and tokenize'
"$FOUNDRY" list --json
# The tensor/tokenizer inventory can be very large; retain it in a file.
"$FOUNDRY" inspect smollm2 --json > "$RUN_DIR/model-inspect.json"
printf 'Full model inspection: %s/model-inspect.json\n' "$RUN_DIR"
"$FOUNDRY" plan smollm2 --context 2048
"$FOUNDRY" tokenize smollm2 --prompt 'The capital of France is'

step 7 'Verify raw completion and chat generation'
"$FOUNDRY" run smollm2 --raw --prompt 'The capital of France is' \
    --context 2048 --max-tokens 32 --metrics-json
"$FOUNDRY" run smollm2 --chat --prompt 'Explain a hash table.' \
    --context 2048 --max-tokens 128 --metrics-json

step 8 'Save environment information'
python3 scripts/environment.py > "$RUN_DIR/environment.json"
printf 'Environment saved to %s/environment.json\n' "$RUN_DIR"

step 9 'Benchmark: two warmups and seven measured requests'
python3 scripts/bench_matrix.py --foundry "$FOUNDRY" --model "$MODEL" \
    --mode warm-model --context 2048 --max-tokens 128 --warmups 2 --repetitions 7 \
    --output-root "$RUN_DIR/benchmarks"

step 10 'Tune CPU threads (up to 300 seconds)'
python3 scripts/tune.py --foundry "$FOUNDRY" --model "$MODEL" \
    --context 2048 --budget-seconds 300
printf '\nPASSED: all diagnostics completed.\nResults: %s\nApply any recommended thread setting manually in the GUI.\n' "$RUN_DIR"
