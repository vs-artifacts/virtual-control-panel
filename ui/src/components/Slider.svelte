<script lang="ts">
    import { onDestroy } from "svelte";
    import { editor } from "../lib/editor.svelte";
    import { hinted } from "../lib/hint.svelte";
    import { openParameterMenu } from "../lib/menu.svelte";
    import {
        beginGesture,
        endGesture,
        resetToDefault,
        setValue,
        type Param,
    } from "../lib/params.svelte";

    // compact: inside a slot (its name and value; the host's name as a hint)
    let { param, compact = false }: { param: Param; compact?: boolean } =
        $props();

    let dragging = false;

    // Our own double-click detection, so its speed is a setting rather than the system's
    const doubleClickDistance = 6;
    let lastClick: { time: number; x: number; y: number } | null = null;

    function clicked(event: MouseEvent) {
        const previous = lastClick;
        lastClick = {
            time: event.timeStamp,
            x: event.clientX,
            y: event.clientY,
        };

        if (
            previous === null ||
            event.timeStamp - previous.time > editor.settings.doubleClickMs ||
            Math.hypot(event.clientX - previous.x, event.clientY - previous.y) >
                doubleClickDistance
        )
            return;

        lastClick = null;

        // The "double-click sets" setting is a placeholder for a value entry; until then both reset
        resetToDefault(param.h);
    }

    function stopDragging() {
        if (!dragging) return;

        dragging = false;
        window.removeEventListener("pointerup", stopDragging);
        window.removeEventListener("pointercancel", stopDragging);
        endGesture(param.h);
    }

    function startDragging(event: PointerEvent) {
        if (event.button !== 0) return;

        // A release that never arrived (e.g. focus moved away mid-drag) must not leave the host in a gesture
        stopDragging();
        dragging = true;

        // The pointer can be released anywhere, not just over the slider
        window.addEventListener("pointerup", stopDragging);
        window.addEventListener("pointercancel", stopDragging);
        beginGesture(param.h);
    }

    function changed(value: number) {
        if (dragging) {
            setValue(param.h, value);
            return;
        }

        // Keyboard steps: one gesture each
        beginGesture(param.h);
        setValue(param.h, value);
        endGesture(param.h);
    }

    function showMenu(event: MouseEvent) {
        event.preventDefault();
        void openParameterMenu(param.h, event.clientX, event.clientY);
    }

    onDestroy(stopDragging);
</script>

<label
    class="slider"
    class:compact
    oncontextmenu={showMenu}
    use:hinted={param.host ? `Host: ${param.host}` : ""}
>
    <span class="name" class:crisp={compact}>{param.name}</span>
    {#if compact}
        <span class="value-text crisp">{param.text}</span>
    {:else}
        <span class="owner">{param.slot}: {param.plugin}</span>
    {/if}
    <input
        type="range"
        min="0"
        max="1"
        step="any"
        value={param.value}
        onpointerdown={startDragging}
        oninput={(event) => changed(event.currentTarget.valueAsNumber)}
        onclick={clicked}
    />
    {#if !compact}
        <span class="value-text">{param.text}</span>
        {#if param.host}
            <span class="host">Host: {param.host}</span>
        {/if}
    {/if}
</label>

<style>
    .slider {
        display: grid;
        grid-template-columns: 1fr auto;
        gap: 2px 12px;
        margin-top: 16px;
        max-width: 420px;
    }

    .name {
        font-weight: 600;
    }

    .owner,
    .host {
        justify-self: end;
        opacity: 0.7;
    }

    input {
        grid-column: 1 / -1;
        width: 100%;
        margin: 4px 0;
    }

    .value-text {
        font-variant-numeric: tabular-nums;
    }

    .slider.compact {
        gap: 0 8px;
        margin-top: 6px;
        max-width: none;
    }

    .compact .name {
        overflow: hidden;
        font-weight: normal;
        text-overflow: ellipsis;
        white-space: nowrap;
    }

    .compact .value-text {
        justify-self: end;
        white-space: nowrap;
    }

    .compact input {
        margin: 2px 0 0;
    }
</style>
