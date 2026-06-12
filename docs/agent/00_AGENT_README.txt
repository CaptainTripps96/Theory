# TheorySequencer Agent README

These files are repo-ready handoff notes for building TheorySequencer with a VS Code coding agent.

Working product name: TheorySequencer
Project package extension: .tseq
C++ namespace: tsq

Fixed technical direction:
- C++20
- JUCE
- Tracktion Engine
- CMake
- Catch2
- VST3 instrument hosting first
- Native JUCE UI
- Versioned .tseq project package
- 960 PPQ ticks per quarter note
- Cross-platform desktop target

How to use these files:
1. Place this folder's .txt files in the project repository, preferably under docs/agent/.
2. Instruct the coding agent to read these files before making changes.
3. Run one prompt at a time from 04_PROMPT_PACK.txt.
4. Do not allow the agent to skip ahead.
5. After each prompt:
   - Build.
   - Run tests.
   - Manually verify acceptance criteria.
   - Commit the working state.
   - Update implementation notes.

Suggested repository location:

docs/
  agent/
    00_AGENT_README.txt
    01_PRODUCT_SPEC.txt
    02_ARCHITECTURE_GUARDRAILS.txt
    03_IMPLEMENTATION_ROADMAP.txt
    04_PROMPT_PACK.txt
    05_AGENT_HANDOFF_NOTES.txt

The most important architectural rule:

TheorySequencer's custom project model is the source of truth.
Tracktion Engine is the playback/plugin backend.
JUCE is the application and UI framework.

The agent should not turn Tracktion's model into the product model, and should not let UI components directly own or mutate musical data.
