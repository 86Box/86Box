# Voodoo ARM64 JIT Port — Agent Guide

## Available Agents

| Agent | Color | Purpose | Tools |
|-------|-------|---------|-------|
| `voodoo-lead` | 🔴 red | Scaffolding, coordination, build/test, Phase 1+2 | Write, Edit, Bash, Serena (all) |
| `voodoo-texture` | 🔵 cyan | Texture fetch, LOD, bilinear, TMU combine (Phase 3) | Write, Edit, Bash, Serena (all) |
| `voodoo-color` | 🟢 green | Color/alpha combine pipeline (Phase 4) | Write, Edit, Bash, Serena (all) |
| `voodoo-effects` | 🟣 magenta | Fog, alpha test/blend, dither, framebuffer write (Phase 5+6) | Write, Edit, Bash, Serena (all) |
| `voodoo-debug` | 🟡 yellow | Validation, debugging, build diagnostics | Bash, Serena (read-only) |
| `voodoo-arch` | 🔵 blue | Architecture research, spec validation | WebSearch, WebFetch, Serena (read-only) |

## When to Use Each Agent

### voodoo-lead (coordinator)
**Use for:**
- Phase 1 scaffolding (header creation, prologue/epilogue)
- Phase 2 pixel loop and depth test
- Build system integration
- Guard changes to existing files
- Coordinating work between other agents
- Testing and validation workflow

**Example:** "Implement Phase 1 scaffolding for ARM64 codegen"

### voodoo-texture (texture specialist)
**Use for:**
- Phase 3 texture fetch implementation
- Perspective-correct W division
- LOD calculation
- Point-sampled and bilinear filtering
- Mirror/clamp texture addressing
- Dual-TMU combine logic

**Example:** "Implement bilinear texture filtering with NEON"

### voodoo-color (color/alpha specialist)
**Use for:**
- Phase 4 color and alpha combine pipeline
- Color select (iter_rgb, tex, color1, lfb)
- Alpha select (iter_a, tex, color1)
- Chroma key test
- Alpha mask test
- Color/alpha multiply and blend operations
- Output clamping

**Example:** "Implement color combine with cc_mselect modes"

### voodoo-effects (effects specialist)
**Use for:**
- Phase 5: Fog (constant, add, mult), alpha test, alpha blend
- Phase 6: Dithering (4x4, 2x2), framebuffer write, depth write
- Per-pixel state increments
- RGB565 packing
- Tiled framebuffer support

**Example:** "Implement fog blend with FOG_MULT mode"

### voodoo-debug (validator)
**Use for:**
- Analyzing build errors or warnings
- Validating ARM64 instruction encodings
- Comparing codegen output to x86-64 reference
- Diagnosing runtime crashes or incorrect behavior
- Reading disassembly (objdump, otool)
- Verifying static assertions

**Example:** "Check the ARM64 encoding for SMULL instruction at line 2950"

### voodoo-arch (architecture expert) **NEW**
**Use for:**
- Validating implementation against official 3dfx specs
- Researching Voodoo hardware behavior
- Finding authoritative documentation (Glide SDK, SST-1 programmer's guide)
- Understanding register bit layouts (fbzMode, fbzColorPath, alphaMode)
- Comparing our logic to real hardware
- Answering questions about Voodoo 1/2/Banshee/3 differences

**Example:** "Does our color combine implementation match the Voodoo spec?"
**Example:** "What's the exact fog blend formula for FOG_MULT mode?"
**Example:** "Find the official 3dfx docs on alpha blend factors"

## Usage Pattern

### For implementation work:
1. Spawn the appropriate implementation agent (lead/texture/color/effects)
2. Agent implements the feature
3. Agent builds with `./scripts/build-and-sign.sh`
4. Agent commits on success
5. If build fails → spawn `voodoo-debug` to diagnose
6. If behavior is unclear → spawn `voodoo-arch` to research specs

### For validation:
1. Spawn `voodoo-debug` to analyze code/logs/disassembly
2. If spec clarification needed → spawn `voodoo-arch` with specific question
3. Debug agent or arch agent reports findings
4. Implementation agent applies fixes if needed

### For spec questions:
1. Spawn `voodoo-arch` with your question
2. Agent searches authoritative sources (WebSearch/WebFetch)
3. Agent compares specs to our implementation
4. Agent reports: ✅ matches, ⚠️ discrepancy, or ❌ bug

## Example Workflows

### Workflow 1: Phase Implementation
```
User: "Implement Phase 5"
→ Spawn voodoo-effects agent
  → Agent implements fog/alpha test/blend
  → Agent builds → SUCCESS
  → Agent commits
→ User tests
→ If rendering wrong:
  → Spawn voodoo-arch: "Validate fog blend formula"
  → Arch agent finds spec
  → Arch agent reports discrepancy
  → Spawn voodoo-effects to fix
```

### Workflow 2: Build Error
```
User: "Build is failing with 'unknown type voodoo_state_t'"
→ Spawn voodoo-debug
  → Debug agent diagnoses: missing include or guard
  → Debug agent reports fix
→ User applies fix or has implementation agent do it
```

### Workflow 3: Spec Validation
```
User: "Is our bilinear filtering correct?"
→ Spawn voodoo-arch
  → Agent searches for Glide SDK bilinear docs
  → Agent reads our codegen_texture_fetch implementation
  → Agent compares weight calculations
  → Agent reports: ✅ matches spec (4-tap blend with >>8 shift)
```

### Workflow 4: Rendering Bug
```
User: "Textures look wrong in Quake"
→ Spawn voodoo-debug to check JIT logs
  → Debug agent sees textureMode values
→ Spawn voodoo-arch: "What does textureMode bit 17 control?"
  → Arch agent: "Bit 17 = minification filter (0=point, 1=bilinear)"
→ Spawn voodoo-texture to verify minification logic
  → Texture agent finds bug in LOD selection
  → Texture agent fixes and commits
```

## Agent Communication

Agents can work together:
- Implementation agents (lead/texture/color/effects) **write code**
- Debug agent **analyzes code** and reports issues (read-only)
- Arch agent **researches specs** and validates correctness (read-only)

When an implementation agent needs validation:
1. User spawns debug or arch agent separately
2. Validation agent reports findings
3. User spawns implementation agent to apply fixes

## Quick Reference

**I want to...** | **Spawn this agent**
---|---
Implement a new phase | lead, texture, color, or effects (depending on phase)
Fix a build error | voodoo-debug
Understand what the hardware does | voodoo-arch
Validate my implementation | voodoo-arch
Debug wrong rendering | voodoo-debug first, then voodoo-arch if needed
Check ARM64 instruction encoding | voodoo-debug
Find official 3dfx documentation | voodoo-arch
Compare our code to x86-64 reference | voodoo-debug
Research Voodoo register layouts | voodoo-arch

## Color Coding (for easy identification in logs)

- 🔴 **Red** = Lead/coordinator
- 🔵 **Cyan** = Texture specialist
- 🟢 **Green** = Color/alpha specialist
- 🟣 **Magenta** = Effects specialist (fog/blend/dither/write)
- 🟡 **Yellow** = Debugger/validator
- 🔵 **Blue** = Architecture expert
