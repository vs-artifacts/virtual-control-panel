<script lang="ts">
    import {
        addBus,
        addSlot,
        editor,
        setCollapsed,
        type Section,
    } from "../lib/editor.svelte";
    import { scrollMemory } from "../lib/layout";
    import { openPageMenu } from "../lib/menu.svelte";
    import { scheduleSnap } from "../lib/pixelText.svelte";
    import { slotDrag } from "../lib/slotDrag.svelte";
    import BayBox from "./BayBox.svelte";
    import BusColumn from "./BusColumn.svelte";
    import SlotBox from "./SlotBox.svelte";

    let { status }: { status: string } = $props();

    // The ghost's text follows the pointer: snap it again as it moves
    $effect(() => {
        void [slotDrag.x, slotDrag.y];

        if (slotDrag.active) scheduleSnap();
    });

    const titles: Record<Section, string> = {
        header: "Header",
        generators: "Generator stage",
        buses: "Buses",
    };

    const structure = $derived(editor.structure);

    // Each generator's place as a drop target: counted without the one being dragged
    const places = $derived.by(() => {
        let place = 0;
        return (structure?.generators ?? []).map((generator) =>
            slotDrag.active && slotDrag.slot === generator.id ? -1 : place++,
        );
    });

    const lineAt = (place: number) =>
        slotDrag.active &&
        slotDrag.target?.bus === 0 &&
        slotDrag.target.index === place;

    function chooseNewGenerator(event: MouseEvent) {
        openPageMenu(
            (structure?.menu.generators ?? []).map((choice) => ({
                text: choice.name,
                enabled: true,
                action: () =>
                    addSlot(0, structure?.generators.length ?? 0, choice.key),
            })),
            event.clientX,
            event.clientY,
        );
    }
</script>

{#snippet bar(section: Section, index: number)}
    <button
        type="button"
        class="bar"
        style:top="{index * 30}px"
        aria-expanded={!editor.collapsed[section]}
        onclick={() => setCollapsed(section, !editor.collapsed[section])}
    >
        <span class="arrow">{editor.collapsed[section] ? "▸" : "▾"}</span>
        <span class="h1">{titles[section]}</span>
    </button>
{/snippet}

<div class="panel" use:scrollMemory={"panel"}>
    <div class="content">
        {@render bar("header", 0)}
        {#if !editor.collapsed.header}
            <div class="header">
                <p id="bridge-status">{status}</p>
                {#if editor.processing !== null}
                    <p>
                        Audio: {editor.processing
                            ? "running"
                            : "not running (the host has stopped calling this plugin)"}
                    </p>
                {/if}
            </div>
        {/if}

        {@render bar("generators", 1)}
        {#if !editor.collapsed.generators && structure !== null}
            <div
                class="row"
                use:scrollMemory={"panel.generators"}
                data-drag-scroll
            >
                <div class="generators" data-drop-bus="0">
                    {#each structure.generators as generator, index (generator.id)}
                        {#if places[index] >= 0 && lineAt(places[index])}
                            <div class="drop-line"></div>
                        {/if}
                        <div data-drop-slot={generator.id}>
                            <SlotBox
                                slot={generator}
                                bus={0}
                                kind="generator"
                            />
                        </div>
                    {/each}
                    {#if lineAt(places.filter((place) => place >= 0).length)}
                        <div class="drop-line"></div>
                    {/if}
                    <button
                        type="button"
                        class="add"
                        onclick={chooseNewGenerator}>+ generator</button
                    >
                </div>
            </div>
        {/if}

        {@render bar("buses", 2)}
        {#if !editor.collapsed.buses && structure !== null}
            <div class="buses">
                <div
                    class="row"
                    use:scrollMemory={"panel.buses"}
                    data-drag-scroll
                >
                    <div class="bus-columns">
                        {#each structure.buses as bus (bus.id)}
                            <BusColumn {bus} />
                        {/each}
                        <button
                            type="button"
                            class="add"
                            onclick={() => addBus(0)}>+ bus</button
                        >
                    </div>
                </div>
                <div class="master">
                    <BusColumn bus={structure.master} master />
                </div>
            </div>
            <div class="bay-row">
                <BayBox />
            </div>
        {/if}
    </div>
</div>

{#if slotDrag.active}
    <div
        class="ghost"
        style:left="{slotDrag.x + 12}px"
        style:top="{slotDrag.y + 12}px"
    >
        <span class="p2 crisp">{slotDrag.label}</span>
    </div>
{/if}

<style>
    .panel {
        height: 100%;
        overflow-x: hidden;
        overflow-y: auto;
        overflow-anchor: none;
    }

    /* Each bar pins under the ones above it */
    .bar {
        position: sticky;
        z-index: 2;
        display: flex;
        align-items: center;
        gap: 8px;
        width: 100%;
        height: 30px;
        padding: 0 16px;
        border: none;
        border-bottom: 1px solid rgba(255, 255, 255, 0.12);
        background: #16181c;
        color: inherit;
        font: inherit;
        text-align: left;
    }

    .arrow {
        width: 12px;
        opacity: 0.7;
    }

    .header {
        box-sizing: border-box;
        height: 200px;
        padding: 12px 16px;
    }

    p {
        margin: 0;
    }

    /* Sideways only; as tall as its contents, so the Panel tab scrolls down */
    .row {
        overflow-x: auto;
        overflow-y: hidden;
        overflow-anchor: none;
    }

    .generators,
    .bus-columns {
        display: flex;
        align-items: flex-start;
        gap: 8px;
        width: max-content;
        padding: 12px 16px;
    }

    /* As tall as BUS X too, so the bus row's scrollbar is at the bottom of the whole bus area */
    .buses {
        display: flex;
        align-items: stretch;
    }

    .buses .row {
        flex: 1;
        min-width: 0;
    }

    /* BUS X stays in view at the right */
    .master {
        flex: none;
        padding: 12px 16px;
        border-left: 1px solid rgba(255, 255, 255, 0.12);
    }

    /* After X.X, lined up with the master's right edge */
    .bay-row {
        display: flex;
        justify-content: flex-end;
        padding: 0 16px 16px;
    }

    .add {
        flex: none;
        width: 180px;
        height: 40px;
        border: 1px dashed rgba(255, 255, 255, 0.2);
        border-radius: 4px;
        background: none;
        color: inherit;
        font: inherit;
        opacity: 0.7;
    }

    .add:hover {
        opacity: 1;
    }

    /* Takes no room (see the bus columns' drop line) */
    .drop-line {
        align-self: stretch;
        width: 0;
        margin: 0 -5px;
        border-left: 2px solid var(--drag);
    }

    .ghost {
        position: fixed;
        z-index: 10;
        padding: 4px 8px;
        border: 1px solid var(--drag);
        border-radius: 4px;
        background: rgba(22, 24, 28, 0.9);
        pointer-events: none;
    }
</style>
