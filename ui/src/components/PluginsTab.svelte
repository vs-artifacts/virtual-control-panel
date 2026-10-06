<script lang="ts">
    import { editor, mutePlugin, openPlugin } from "../lib/editor.svelte";
</script>

{#each editor.plugins as plugin (plugin.slot)}
    <div class="row">
        <div class="text">
            <span class="name">{plugin.label}: {plugin.name}</span>
            <span class="status">{plugin.status}</span>
        </div>
        {#if plugin.canMute}
            <label
                class="mute"
                title="Silences this generator's output (30 ms fade); it keeps running, so the load stays the same"
            >
                <input
                    type="checkbox"
                    checked={plugin.muted}
                    onchange={(event) =>
                        mutePlugin(plugin.slot, event.currentTarget.checked)}
                />
                Mute
            </label>
        {/if}
        <button
            type="button"
            disabled={!plugin.canOpen}
            onclick={() => openPlugin(plugin.slot)}>Open window</button
        >
    </div>
{:else}
    <p class="status">No hosted plugins</p>
{/each}

<style>
    .row {
        display: flex;
        align-items: center;
        gap: 12px;
        padding: 6px 0;
        border-bottom: 1px solid rgba(255, 255, 255, 0.1);
    }

    .text {
        display: flex;
        flex: 1;
        flex-direction: column;
        min-width: 0;
    }

    .name {
        font-weight: 500;
    }

    .mute {
        display: flex;
        align-items: center;
        gap: 5px;
    }

    input {
        margin: 0;
    }

    .status {
        opacity: 0.7;
        overflow-wrap: anywhere;
    }

    p {
        margin: 0;
    }
</style>
