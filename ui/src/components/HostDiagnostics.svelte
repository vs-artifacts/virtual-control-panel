<script lang="ts">
    import { editor } from "../lib/editor.svelte";
</script>

<section>
    <h2>Keys forwarded to the host (test build)</h2>
    {#if editor.keyLog.length > 0}
        <ol id="key-log">
            {#each editor.keyLog as line, index (index)}
                <li>{line}</li>
            {/each}
        </ol>
    {:else}
        <p class="waiting">
            None yet: type on the page (with and without a mouse button held)
        </p>
    {/if}
</section>

<section>
    <h2>Windows around the editor (test build)</h2>
    {#if editor.windows.length > 0}
        <ol id="window-chain">
            {#each editor.windows as line, index (index)}
                <li>{line}</li>
            {/each}
        </ol>
    {:else}
        <p class="waiting">Not reported</p>
    {/if}
</section>

<style>
    section {
        margin-top: 18px;
        padding-top: 10px;
        border-top: 1px solid rgba(255, 255, 255, 0.15);
    }

    h2 {
        margin: 0 0 6px;
        font-size: 16px;
        font-weight: 500;
    }

    ol {
        margin: 0;
        padding-left: 20px;
        font-size: 13px;
        font-variant-numeric: tabular-nums;
        overflow-wrap: anywhere;
    }

    .waiting {
        margin: 0;
        opacity: 0.7;
    }
</style>
