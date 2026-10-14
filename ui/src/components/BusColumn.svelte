<script lang="ts">
    import {
        addBus,
        addSlot,
        editor,
        removeBus,
        type BusInfo,
    } from "../lib/editor.svelte";
    import { openPageMenu } from "../lib/menu.svelte";
    import { hinted } from "../lib/hint.svelte";
    import { slotDrag } from "../lib/slotDrag.svelte";
    import SlotBox from "./SlotBox.svelte";

    let { bus, master = false }: { bus: BusInfo; master?: boolean } = $props();

    // "BUS 2" numbers its stages 2.0 and 2.X; the master's are X.0 and X.X
    const number = $derived(bus.label.replace(/^BUS /, ""));
    const parentLabel = $derived(
        editor.structure?.buses.find((entry) => entry.id === bus.parent)
            ?.label ?? "",
    );

    // Each slot's place as a drop target: counted without the slot being dragged
    const places = $derived.by(() => {
        let place = 0;
        return bus.slots.map((slot) =>
            slotDrag.active && slotDrag.slot === slot.id ? -1 : place++,
        );
    });

    const lineAt = (place: number) =>
        slotDrag.active &&
        slotDrag.target?.bus === bus.id &&
        slotDrag.target.index === place;

    function chooseNewSlot(event: MouseEvent) {
        const choices = [
            { key: "", name: "Empty" },
            ...(editor.structure?.menu.fx ?? []),
        ];
        openPageMenu(
            choices.map((choice) => ({
                text: choice.name,
                enabled: true,
                action: () => addSlot(bus.id, bus.slots.length, choice.key),
            })),
            event.clientX,
            event.clientY,
        );
    }
</script>

<div class="bus" class:master data-drop-bus={bus.id}>
    <div class="bus-header">
        <span class="title h3">{bus.label}</span>
        {#if bus.parent !== 0}
            <span
                class="sub"
                use:hinted={`A sub-bus: it feeds ${parentLabel}'s end stage.`}
                >→ {parentLabel.replace(/^BUS /, "")}.X</span
            >
        {/if}
        <span class="spacer"></span>
        {#if !master && bus.parent === 0}
            <button
                type="button"
                use:hinted={`Adds a sub-bus to ${bus.label}.`}
                onclick={() => addBus(bus.id)}>+ sub</button
            >
        {/if}
        {#if !master}
            <button
                type="button"
                use:hinted={`Removes ${bus.label}, its slots and its sub-buses.`}
                onclick={() => removeBus(bus.id)}>×</button
            >
        {/if}
    </div>

    <div class="stage p2"><span class="crisp">{number}.0</span></div>

    {#each bus.slots as slot, index (slot.id)}
        {#if places[index] >= 0 && lineAt(places[index])}
            <div class="drop-line"></div>
        {/if}
        <div data-drop-slot={slot.id}>
            <SlotBox {slot} bus={bus.id} kind="fx" />
        </div>
    {/each}
    {#if lineAt(places.filter((place) => place >= 0).length)}
        <div class="drop-line"></div>
    {/if}

    <button type="button" class="add" onclick={chooseNewSlot}>+ slot</button>
    <div class="stage p2"><span class="crisp">{number}.X</span></div>
</div>

<style>
    .bus {
        display: flex;
        flex-direction: column;
        gap: 6px;
        width: 180px;
        flex: none;
    }

    .bus-header {
        display: flex;
        align-items: center;
        gap: 6px;
        height: 24px;
    }

    .sub {
        opacity: 0.6;
    }

    .spacer {
        flex: 1;
    }

    .bus-header button,
    .add {
        padding: 0 6px;
        border: 1px solid rgba(255, 255, 255, 0.2);
        border-radius: 3px;
        background: none;
        color: inherit;
        font: inherit;
        opacity: 0.7;
    }

    .bus-header button:hover,
    .add:hover {
        opacity: 1;
    }

    .add {
        height: 28px;
        border-style: dashed;
    }

    /* b.0 and b.X: fixed places for utilities later, empty for now */
    .stage {
        display: flex;
        align-items: center;
        height: 24px;
        padding: 0 8px;
        border: 1px dashed rgba(255, 255, 255, 0.12);
        border-radius: 4px;
        opacity: 0.45;
    }

    /* Takes no room, so showing it never moves the slots under the pointer */
    .drop-line {
        height: 0;
        margin: -4px 0;
        border-top: 2px solid var(--drag);
    }
</style>
