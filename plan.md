# Phoenix-8086 Project Plan

## Objective

Create a single, polished master project specification from the scattered notes in `projectdetails.md`, then use that brief as the source of truth for implementation, division of labor, and future scope control.

## Work Plan

1. Rewrite the concept into a formal project brief with clear sections and consistent terminology.
2. Separate baseline requirements from stretch goals so the team has a feasible delivery target.
3. Define the architecture in layers: bootloader, stage 2 loader, kernel core, scheduler, interrupts, drivers, and services.
4. Document the TCB layout, thread lifecycle, and scheduler expectations so implementation details are unambiguous.
5. Add milestones and success criteria so progress can be tracked in a structured way.
6. Keep the document concise enough to be maintained, but complete enough to act as the team reference.

## Team Usage

* Treat `projectdetails.md` as the master specification.
* Use this plan as the implementation roadmap.
* Split work by subsystem once the baseline architecture is approved.

## Short-Term Next Steps

1. Review the rewritten master brief for accuracy and scope.
2. Confirm which stretch goals are actually intended for the first release.
3. Start implementation from the boot path and kernel bring-up milestones.