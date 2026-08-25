# Contributing to SimpleQuest

Thanks for considering it. SimpleQuest is a solo-maintained project, and contributions are genuinely welcome - with the caveat that a small project has a small review budget, so the most useful thing you can do before writing code is talk to me about it.

This document covers what's in scope, what isn't, and what a pull request should carry. It's a guideline that explains how to keep contributions meaningful and efficient, plus the way a contribution falls under the licensing terms, but isn't a contract or agreement. If something here is unclear or seems wrong, say so - the [Discord server](https://discord.gg/PN9kzPypeS) is the fastest way to reach me.

---

## Where the project is right now

Pre-1.0. The public API is not frozen and won't be until v1.0.0 - see the roadmap table in the [README](README.md). Minor versions still carry breaking changes, documented in the [CHANGELOG](CHANGELOG.md). That's relevant to contributors in two ways: a PR against a surface I'm about to rework is wasted effort, and a PR that changes a public surface needs a CHANGELOG entry describing the migration.

---

## Reporting bugs

Open a [GitHub issue](https://github.com/TheGeebus/SimpleQuest/issues). Please include:

- The engine version (5.6, 5.7, 5.8, etc.).
- A minimal reproduction - ideally the smallest questline graph that shows the problem.
- Relevant output from the `LogSimpleQuest` and `LogSimpleCore` log categories. Both support per-channel verbosity; raising the relevant channel to `Verbose` usually produces the decisive line.
- Whether it reproduces in the `SimpleQuestDemo` host project, or only in yours.

The last one matters more than it sounds. "Only in mine" is still a valid bug report - it usually means the framework is making an assumption about your project that it shouldn't - but knowing which side it's on saves a round trip.

---

## Talk before you build

For anything beyond a small, self-evident fix, open an issue or a Discord thread first and describe the change you have in mind.

This isn't gatekeeping. SimpleQuest's design decisions are reasoned through before they're coded, and a fair number of them aren't necessarily visible from the call site - the manager's protected-virtual seam, the event deduplication contract, the ordering guarantee that state completes before an event broadcasts. A PR can be well-written, well-tested, and still unmergeable because it violates one of the less apparent design tenets. I'd rather spend ten minutes telling you up front where the landmines are buried than have you find out after a weekend of work.

I'm always glad to discuss the direction of the framework, happy to walk through my rationale behind various decisions, and open to being challenged that there may be a better way. No implementation is sacred, and an idea that yields a more modular framework that's easier to build with and scales well has a great chance of being adopted. 

The other reason is simple: I may already be partway into the same problem. It's happened that people have independently built things SimpleQuest is walking toward, and in those cases the conversation was often worth as much as the code, yielding better solutions than those offered in isolation.

The fastest way to get in touch with me is through the [Simple Quest Discord server](https://discord.gg/PN9kzPypeS), but I'll also make every attempt to respond to Issues opened via the GitHub repo.

---

## Scope: what I can and can't merge

SimpleQuest is open-core. The plugins in this repository are MIT and will stay MIT. A separate paid **Pro** module is planned for after 1.0 - the roadmap table in the README marks those rows explicitly. Today that's **multiplayer replication** and **GAS integration**.

I'm telling you this before you write code, not after you submit it.

**Welcome:**

- Bug fixes, with a test that fails without them where the subsystem is testable.
- Engine compatibility fixes for 5.6 and up.
- New objective types, reward adapters, and data-format adapters. These are the designed extension points; adding one shouldn't require touching the framework, and if it does, that's a framework bug worth reporting on its own.
- Documentation, including corrections to the README and to source comments. Source comments in the public headers are adopter-facing documentation and are held to the same standard as the README.
- Editor ergonomics - authoring friction you hit in practice.
- **Anything that removes an obstacle to scaling.** Assumptions of single-player authority in runtime paths, state that can only be reconstructed from live object identity rather than from data, mutation paths that bypass the manager seam so an authority check would have nowhere to live. Work that makes the core replication-*ready* is squarely in scope and I'd be glad to have it.

**Can't merge:**

- Replication machinery itself - `NetSerialize` implementations, replicated state containers, server RPCs, join-in-progress reconciliation.
- GAS integration - GameplayEffect-based rewards, Ability-driven objectives, Gameplay Event trigger bridges.

Those two are the intended commercial tier of an open-core project maintained by one person. That's the whole reason, and I'd rather state it plainly than dress it up. It isn't a judgment about the quality of the work.

If you've built either one and want to keep using it, MIT lets you: maintain it as a fork or as a separate plugin that depends on SimpleQuest, with no obligation to me and no permission needed. I'm happy to answer questions about the seams it should hook into. What I can't do is take it into this repository.

If you're unsure which side of the line something falls on, ask. The boundary is about those two features, not about ambition.

---

## Licensing of contributions

**Inbound equals outbound.** By opening a pull request you agree that your contribution is licensed under the same [MIT License](LICENSE) as the rest of the repository. There's no CLA and no copyright assignment - you keep your copyright, and your code stays MIT permanently. I can't relicense it out from under you.

Two consequences to be explicit about, since open-core makes people reasonably wary:

- The Pro module will link against this MIT core, which is exactly what MIT permits. If your contribution is in the core, paid modules can and will be built on top of it. That's true of every MIT dependency in your engine, but I just want to make it explicit rather than leave it inferred.
- Because there's no CLA, contributed code can't be moved *into* the closed module. Anything you contribute here stays here, in the open, under MIT.

Don't contribute code you don't have the right to license - work owned by an employer, code lifted from a proprietary codebase, or anything under a copyleft license.

**Sign off your commits.** Add the `Signed-off-by` line by committing with `-s`:

```
git commit -s -m "your message"
```

That line means you're agreeing to the [Developer Certificate of Origin](https://developercertificate.org/) - a short statement that you wrote the code or otherwise have the right to submit it, and that you understand it will be public under this project's license. It grants me nothing beyond what MIT already gives; it's a record that the code was yours to give, kept next to the code rather than in a document nobody re-reads.

It's not a CLA and doesn't quietly become one. Everything above still holds: no assignment, your copyright stays yours, your contribution stays MIT. I ask for it now because the first merge sets the precedent, and one line per commit is trivial to adopt while retrofitting it across a dozen contributors later is not.

If you forget the flag, I'll say so rather than bounce the PR.

---

## What a pull request should carry

**Build it.** Development Editor target, UE 5.6 at minimum. If the change touches version-sensitive engine API, say in the PR description which versions you actually built against - "5.6 only" is a fine answer and much better than silence.

**Test it where the code is testable.** The automation tests live under `SimpleQuest.*` in the Session Frontend (Window > Developer Tools > Session Frontend > Automation), and under `Plugins/SimpleQuest/Source/*/Private/Tests/` in the tree. Run the whole `SimpleQuest` group before submitting, not just your own test.

**Watch your test fail.** This is the one house rule I'd ask you to take seriously. A green test proves nothing until you've seen it go red for the right reason - break the thing it covers, confirm the test catches it, then put it back. More than one test in this repository sat green while proving nothing, and each was found this way rather than by reading them. If you add a test, sabotage it once. If the sabotage doesn't fail it, the test is measuring the wrong thing. Tests that are easily provable this way are weighed heavily when I review a PR.

Some parts of the runtime aren't unit-testable yet - anything needing a live subsystem collection is currently verified in PIE, and building a proper runtime harness is on the list. If your change lands there, describe your manual verification steps instead. That's an honest answer, not a shortfall.

**Keep it to one thing.** A focused PR gets reviewed. A PR that fixes a bug, renames three things, and reformats a file gets deferred, because the interesting change is buried.

**Note breaking changes.** If a public surface moves, say so in the PR description with the before and after. It needs a CHANGELOG entry, and I'd rather write it from your description than reverse-engineer it from the diff.

---

## House style

Match the surrounding code. Beyond that, a short list of things that are consistent across the codebase and easier to get right the first time:

- Lines wrap at roughly 130-150 characters. Prefer keeping an expression or a parameter list on one line over splitting it to stay under the limit.
- `/** */` blocks document declarations. Stacked `//` comments explain steps inside function bodies.
- Comments in public headers are written for adopters, not for maintainers - explain the behavior and the reason for it, not the sprint it came from.
- American spellings throughout: `color`, `behavior`, `-ize`.
- Blueprint-facing APIs take `USTRUCT` parameters and Gameplay Tag pickers, not wildcards and `FName` strings. If a designer will touch it, it should be pickable.
- Log something. Data-handling code in particular is nearly invisible at both editor time and runtime, and a well-placed `Verbose` line is the difference between a five-minute diagnosis and an afternoon.
- Commit messages: plain and short. The one trailer I do want is the `Signed-off-by` line described above; please leave off generated-by and co-authored-by.

---

## On AI-assisted contributions

Use whatever tools you like. The only requirement is that you've read the code you're submitting and can explain why it works in review. A PR you can't defend is one I have to reverse-engineer alone, which costs more review budget than the contribution is worth - that's the whole of the concern, and it applies equally to code copied from a forum post.

---

## Getting in touch

- **Bugs and feature requests:** [GitHub issues](https://github.com/TheGeebus/SimpleQuest/issues).
- **Design discussion, questions, or a quick sanity check before you build something:** the [Simple Quest Discord server](https://discord.gg/PN9kzPypeS). Faster than issues, and better suited to anything open-ended. This is generally the quickest way to get in touch with me.

If you're using SimpleQuest in a project, I'd like to hear about it even if you never open a PR. Knowing where it breaks in real use has shaped this roadmap more than anything I've found on my own. Even just sharing how you're using it is enormously instructive at this stage. Plus I just love seeing what everyone is building!

Thanks again for considering a contribution and honestly just for building with it. I hope it helps to bring your visions to life.

*- TheGeebus*
