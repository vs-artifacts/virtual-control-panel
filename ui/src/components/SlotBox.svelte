<script lang="ts">
    import {
        editor,
        fillSlot,
        mutePlugin,
        openPlugin,
        removeSlot,
        setGeneratorBus,
        type GeneratorInfo,
        type SlotInfo,
    } from "../lib/editor.svelte";
    import { openPageMenu } from "../lib/menu.svelte";
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
</script>

<div class="slot" class:dragging={slotDrag.active && slotDrag.slot === slot.id}>
    <div
        class="slot-header"
        role="button"
        tabindex="-1"
        aria-roledescription="drag handle"
        title="Drag to move"
        onpointerdown={(event) =>
            beginSlotDrag(event, slot.id, kind, `${slot.label} ${slot.name}`)}
    >
        <span class="label">{slot.label}</span>
        <span class="name">{slot.name}</span>
        <button
            type="button"
            class="remove"
            title="Remove this slot"
            onclick={() => removeSlot(slot.id)}>×</button
        >
    </div>

    <div class="slot-body">
        {#if slot.plugin === ""}
            <button type="button" onclick={choosePlugin}>Choose plugin…</button>
        {:else}
            {#if slot.status}
                <div class="status">{slot.status}</div>
            {/if}
            <div class="actions">
                <button
                    type="button"
                    disabled={!slot.canOpen}
                    onclick={() => openPlugin(slot.id)}>Open</button
                >
                {#if slot.canMute}
                    <label
                        title="Silences this generator's output (30 ms fade); it keeps running, so the load stays the same"
                    >
                        <input
                            type="checkbox"
                            checked={slot.muted}
                            onchange={(event) =>
                                mutePlugin(
                                    slot.id,
                                    event.currentTarget.checked,
                                )}
                        />
                        Mute
                    </label>
                {/if}
            </div>
            {#if fedBuses !== null}
                <div class="feeds" title="The buses this generator feeds">
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
                                )}>{target.label}</button
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
        font-family: "m5x7", monospace;
        font-size: 16px;
        line-height: 1;
    }

    .slot.dragging {
        opacity: 0.35;
    }

    .slot-header {
        display: flex;
        align-items: center;
        gap: 6px;
        height: 40px;
        box-sizing: border-box;
        padding: 0 4px 0 8px;
        border-bottom: 1px solid rgba(255, 255, 255, 0.1);
        cursor: grab;
        user-select: none;
        touch-action: none;
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

    label {
        display: flex;
        align-items: center;
        gap: 4px;
    }

    input[type="checkbox"] {
        margin: 0;
    }

    button {
        font: inherit;
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
        border-color: #8fa2ff;
        background: rgba(143, 162, 255, 0.2);
        opacity: 1;
    }
</style>
