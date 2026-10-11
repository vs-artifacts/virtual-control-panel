<script lang="ts">
    import { controls } from "../lib/params.svelte";
    import Slider from "./Slider.svelte";

    // The bay's controls (not in a slot yet): after the master, until bays become slots
    const params = $derived(controls.params.filter((param) => param.s === 0));
</script>

{#if params.length > 0}
    <div class="bay">
        <div class="bay-header">
            <span class="label">BAY 1</span>
            <span>FL plugin, through relays</span>
        </div>
        <div class="controls">
            {#each params as param (param.h)}
                <Slider {param} compact />
            {/each}
        </div>
    </div>
{/if}

<style>
    .bay {
        border: 1px solid rgba(255, 255, 255, 0.14);
        border-radius: 4px;
        background: rgba(255, 255, 255, 0.05);
        font-family: "m5x7", monospace;
        font-size: 16px;
        line-height: 1;
    }

    .bay-header {
        display: flex;
        align-items: center;
        gap: 6px;
        height: 40px;
        box-sizing: border-box;
        padding: 0 8px;
        border-bottom: 1px solid rgba(255, 255, 255, 0.1);
    }

    .label {
        opacity: 0.6;
    }

    /* 4 columns of 4, each a slot's width */
    .controls {
        display: grid;
        grid-template-columns: repeat(4, 164px);
        grid-template-rows: repeat(4, auto);
        grid-auto-flow: column;
        column-gap: 16px;
        padding: 0 8px 8px;
    }
</style>
