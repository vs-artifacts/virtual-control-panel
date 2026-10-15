<script lang="ts">
    import {
        editor,
        fillSlot,
        openPlugin,
        removeSlot,
        setEndsPath,
        setGeneratorBus,
        setSlotMix,
        setSlotState,
        type GeneratorInfo,
        type SlotInfo,
    } from "../lib/editor.svelte";
    import type { SlotStateName } from "../lib/bridge";
    import { openPageMenu } from "../lib/menu.svelte";
    import { hinted } from "../lib/hint.svelte";
    import { controls } from "../lib/params.svelte";
    import {
        beginSlotDrag,
        slotDrag,
        type DragKind,
    } from "../lib/slotDrag.svelte";
    import Slider from "./Slider.svelte";

    // bus: the bus it's in (0 for a generator)
    let {
        slot,
        bus,
        kind,
    }: { slot: SlotInfo | GeneratorInfo; bus: number; kind: DragKind } =
        $props();

    const params = $derived(
        controls.params.filter((param) => param.s === slot.id),
    );
    const fedBuses = $derived("buses" in slot ? slot.buses : null);

    function choosePlugin(event: MouseEvent) {
        const choices = editor.structure?.menu.fx ?? [];
        openPageMenu(
            choices.map((choice) => ({
                text: choice.name,
                enabled: true,
                action: () => fillSlot(bus, slot.id, choice.key),
            })),
            event.clientX,
            event.clientY,
        );
    }

    // The state buttons: the selected one again goes back to ON
    const isGenerator = $derived(kind === "generator");
    const states = $derived<
        { state: SlotStateName; text: string; hint: string }[]
    >([
        {
            state: "mute",
            text: "M",
            hint: isGenerator
                ? "MUTE: silent; the plugin keeps running. Again: back to ON."
                : "MUTE: the dry sound plays; the plugin keeps running. Again: back to ON.",
        },
        {
            state: "bypass",
            text: "B",
            hint: isGenerator
                ? "BYPASS: silent; the plugin stops and is reset (no old tail when it comes back). Again: back to ON."
                : "BYPASS: the dry sound plays, delayed by the plugin's latency; the plugin stops and is reset (no old tail when it comes back). Again: back to ON.",
        },
        {
            state: "off",
            text: "OFF",
            hint: `OFF: out of the processing graph, so its latency no longer counts (the output dips while it changes). Again: back to ON.\nRight-click: end signal path when off (${slot.endsPath ? "on" : "off"}).`,
        },
    ]);

    function chooseState(state: SlotStateName) {
        setSlotState(slot.id, slot.state === state ? "on" : state);
    }

    function offMenu(event: MouseEvent) {
        event.preventDefault();

        if (!slot.hasStates) return;

        openPageMenu(
            [
                {
                    text: `${slot.endsPath ? "✓ " : ""}End signal path when off`,
                    enabled: true,
                    hint: isGenerator
                        ? "For a generator this has no further effect yet."
                        : "While OFF, every later slot in this bus is cut off too: not run, not connected (no CPU, no latency).",
                    action: () => setEndsPath(slot.id, !slot.endsPath),
                },
            ],
            event.clientX,
            event.clientY,
        );
    }

    const mixHint = $derived(
        `MIX ${Math.round(slot.mix * 100)}%: ${isGenerator ? "the generator's level" : "dry/wet, linear"}. Double-click: 100%.`,
    );
</script>

<div
    class="slot p2"
    class:dragging={slotDrag.active && slotDrag.slot === slot.id}
    class:cut-off={slot.cutOff}
>
    <div
        class="slot-header"
        role="button"
        tabindex="-1"
        aria-roledescription="drag handle"
        use:hinted={"Drag to move."}
        onpointerdown={(event) =>
            beginSlotDrag(event, slot.id, kind, `${slot.label} ${slot.name}`)}
    >
        <div class="header-row">
            <span class="label crisp">{slot.label}</span>
            <span class="name crisp">{slot.name}</span>
            <button
                type="button"
                class="remove crisp"
                use:hinted={"Removes this slot, with its plugin's settings."}
                onclick={() => removeSlot(slot.id)}>×</button
            >
        </div>
        <div class="header-row controls p3">
            {#each states as choice (choice.state)}
                <button
                    type="button"
                    class="state"
                    class:selected={slot.state === choice.state}
                    class:ends={choice.state === "off" && slot.endsPath}
                    data-state={choice.state}
                    disabled={!slot.hasStates}
                    aria-pressed={slot.state === choice.state}
                    use:hinted={choice.hint}
                    onclick={() => chooseState(choice.state)}
                    oncontextmenu={choice.state === "off" ? offMenu : undefined}
                    ><span class="crisp">{choice.text}</span></button
                >
            {/each}
            <label class="mix" use:hinted={mixHint}>
                <span class="crisp">MIX</span>
                <input
                    type="range"
                    min="0"
                    max="1"
                    step="0.01"
                    aria-label="Mix"
                    disabled={!slot.hasStates}
                    value={slot.mix}
                    oninput={(event) =>
                        setSlotMix(slot.id, event.currentTarget.valueAsNumber)}
                    ondblclick={() => setSlotMix(slot.id, 1)}
                />
            </label>
        </div>
    </div>

    <div class="slot-body">
        {#if slot.cutOff}
            <div class="note crisp">
                Cut off: an earlier slot ends the signal path
            </div>
        {/if}
        {#if slot.plugin === ""}
            <button type="button" onclick={choosePlugin}
                ><span class="crisp">Choose plugin…</span></button
            >
        {:else}
            {#if slot.status}
                <div class="status crisp">{slot.status}</div>
            {/if}
            <div class="actions">
                <button
                    type="button"
                    disabled={!slot.canOpen}
                    use:hinted={"Opens the plugin's own window."}
                    onclick={() => openPlugin(slot.id)}
                    ><span class="crisp">Open</span></button
                >
            </div>
            {#if fedBuses !== null}
                <div
                    class="feeds"
                    use:hinted={"The buses this generator feeds."}
                >
                    {#each editor.structure?.buses ?? [] as target (target.id)}
                        <button
                            type="button"
                            class="chip"
                            aria-pressed={fedBuses.includes(target.id)}
                            onclick={() =>
                                setGeneratorBus(
                                    slot.id,
                                    target.id,
                                    !fedBuses.includes(target.id),
                                )}
                            ><span class="crisp">{target.label}</span></button
                        >
                    {/each}
                </div>
            {/if}
            {#each params as param (param.h)}
                <Slider {param} compact />
            {/each}
        {/if}
    </div>
</div>

<style>
    .slot {
        width: 180px;
        box-sizing: border-box;
        border: 1px solid rgba(255, 255, 255, 0.14);
        border-radius: 4px;
        background: rgba(255, 255, 255, 0.05);
    }

    .slot.dragging {
        opacity: 0.35;
    }

    /* Greyed; its own buttons still work */
    .slot.cut-off .slot-body,
    .slot.cut-off .header-row:first-child {
        opacity: 0.4;
    }

    /* Two 20 px rows: label, name and x; the state buttons and MIX */
    .slot-header {
        display: flex;
        flex-direction: column;
        height: 40px;
        box-sizing: border-box;
        padding: 0 4px 0 8px;
        border-bottom: 1px solid rgba(255, 255, 255, 0.1);
        cursor: grab;
        user-select: none;
        touch-action: none;
    }

    .header-row {
        display: flex;
        align-items: center;
        gap: 6px;
        height: 20px;
    }

    .controls {
        gap: 2px;
    }

    /* Text only; the selected state in the accent */
    .state {
        padding: 0 3px;
        border: none;
        border-radius: 0;
        background: none;
        color: var(--mid-gray);
        cursor: default;
    }

    .state:not(:disabled):hover {
        color: #fff;
    }

    .state.selected {
        color: var(--accent);
    }

    /* "End signal path when off" is on: a line under OFF */
    .state.ends {
        cursor: help;
        box-shadow: inset 0 -1px 0 currentColor;
    }

    .mix {
        display: flex;
        flex: 1;
        align-items: center;
        gap: 4px;
        min-width: 0;
        margin-left: 4px;
        color: var(--mid-gray);
    }

    .mix input {
        flex: 1;
        min-width: 0;
        height: 12px;
        margin: 0;
    }

    .note {
        opacity: 0.8;
    }

    .label {
        opacity: 0.6;
    }

    .name {
        flex: 1;
        min-width: 0;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
    }

    .slot-body {
        display: flex;
        flex-direction: column;
        gap: 6px;
        padding: 6px 8px 8px;
    }

    .status {
        opacity: 0.6;
        overflow-wrap: anywhere;
    }

    .actions,
    .feeds {
        display: flex;
        flex-wrap: wrap;
        align-items: center;
        gap: 6px;
    }

    button {
        font: inherit;
    }

    /* Pixel text can't be translucent (see .crisp): fade the whole button */
    button:disabled {
        color: inherit;
        opacity: 0.45;
    }

    .remove {
        padding: 0 6px;
        border: none;
        background: none;
        color: inherit;
        opacity: 0.6;
    }

    .remove:hover {
        opacity: 1;
    }

    .chip {
        padding: 1px 5px;
        border: 1px solid rgba(255, 255, 255, 0.25);
        border-radius: 3px;
        background: none;
        color: inherit;
        opacity: 0.6;
    }

    .chip[aria-pressed="true"] {
        border-color: var(--accent);
        background: var(--accent-soft);
        opacity: 1;
    }
</style>
