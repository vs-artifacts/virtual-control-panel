<script lang="ts">
    import { onDestroy } from "svelte";
    import { editor } from "../lib/editor.svelte";
    import { openParameterMenu } from "../lib/menu.svelte";
    import {
        beginGesture,
        endGesture,
        resetToDefault,
        setValue,
        type Param,
    } from "../lib/params.svelte";

    let { param }: { param: Param } = $props();

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

<label class="slider" oncontextmenu={showMenu}>
    <span class="name">{param.name}</span>
    <span class="owner">{param.slot}: {param.plugin}</span>
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
    <span class="value-text">{param.text}</span>
    {#if param.host}
        <span class="host">Host: {param.host}</span>
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
        font-weight: 500;
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
</style>
