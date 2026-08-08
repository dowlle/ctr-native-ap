# AI usage in CTR Archipelago

I use Anthropic's Claude through Claude Code while developing CTR Archipelago. It helps me write and review code, investigate bugs, and keep the native client and apworld in step. I make the design decisions, choose what enters a release, and remain responsible for the result.

I have ADHD, and AI assistance helps me carry ideas through the long implementation and verification work needed to finish them. That is the practical reason I use it.

## Verification

AI-generated work is treated like any other untrusted contribution. Changes are checked against the game's source and the project's specification rather than accepted because they compile or make a test pass. Apworld releases run through the Archipelago fuzz matrix, and native releases are tested in game on real seeds. Release notes state what was and was not verified for that release.

## Art and existing work

The project does not use AI-generated art. In-game markers and tracker icons come from the original game's models or from credited, licensed sources. CTR Archipelago builds on the work of the original Crash Team Racing developers, the CTR decompilation community, Archipelago contributors, and Icebound777 and Taor's randomizer design. Their work is credited in the README and third-party notices.

## Environmental footprint

AI systems use electricity and water, and the wider data-center buildout is a real concern. Published measurements do not support one universal per-request number because models, hardware, data centers, and prompts differ. I keep this project non-commercial and use AI because it makes sustained development possible for me, while recognizing that other people may weigh that tradeoff differently.

