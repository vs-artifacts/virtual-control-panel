<script lang="ts">
    import { chooseKeyRoute, editor, keyRoutes } from "../lib/editor.svelte";

    const routeTexts: Record<(typeof keyRoutes)[number], string> = {
        A: "A: FL's control message to the plugin panel, else posted",
        B: "B: FL's control message up the window chain",
        C: "C: the key message, delivered at once to the plugin panel",
        D: "D: the key message, delivered at once to FL's main window",
    };
</script>

<section>
    <h2>Keys forwarded to the host (test build)</h2>
    <fieldset>
        <legend>Key route while a mouse button is held (FL only)</legend>
        {#each keyRoutes as route (route)}
            <label class="option">
                <input
                    type="radio"
                    name="keyRoute"
                    checked={editor.keyRoute === route}
                    onchange={() => chooseKeyRoute(route)}
                />
                {routeTexts[route]}
            </label>
        {/each}
    </fieldset>
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

    fieldset {
        margin: 0 0 8px;
        padding: 0;
        border: none;
    }

    legend {
        padding: 0 0 2px;
    }

    .option {
        display: flex;
        align-items: center;
        gap: 6px;
        padding: 1px 0;
    }

    input {
        margin: 0;
    }

    .waiting {
        margin: 0;
        opacity: 0.7;
    }
</style>
