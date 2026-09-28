<script lang="ts">
    import { chooseMenuItem, type MenuItem } from "../lib/menu.svelte";
    import MenuList from "./MenuList.svelte";

    let { items }: { items: MenuItem[] } = $props();
</script>

<div class="menu-list" role="menu">
    {#each items as item, index (index)}
        {#if item.kind === "separator"}
            <div class="separator" role="separator"></div>
        {:else if item.kind === "header"}
            <div class="header">{item.text}</div>
        {:else if item.kind === "submenu"}
            <div class="submenu">
                <button
                    type="button"
                    role="menuitem"
                    aria-haspopup="menu"
                    disabled={!item.enabled}
                >
                    <span class="check"></span>{item.text}<span class="arrow"
                        >▸</span
                    >
                </button>
                {#if item.enabled}
                    <div class="child">
                        <MenuList items={item.items} />
                    </div>
                {/if}
            </div>
        {:else}
            <button
                type="button"
                role="menuitem"
                disabled={!item.enabled}
                onclick={() => chooseMenuItem(item.id)}
            >
                <span class="check">{item.checked ? "✓" : ""}</span>{item.text}
            </button>
        {/if}
    {/each}
</div>

<style>
    .menu-list {
        min-width: 180px;
        padding: 4px 0;
        background: rgba(30, 34, 64, 0.92);
        border: 1px solid rgba(255, 255, 255, 0.18);
        border-radius: 6px;
        box-shadow: 0 6px 24px rgba(0, 0, 0, 0.45);
    }

    button {
        display: flex;
        align-items: center;
        width: 100%;
        padding: 4px 12px 4px 4px;
        border: none;
        background: none;
        color: inherit;
        font: inherit;
        text-align: left;
        white-space: nowrap;
    }

    button:not(:disabled):hover,
    .submenu:hover > button:not(:disabled) {
        background: rgba(255, 255, 255, 0.12);
    }

    button:disabled {
        opacity: 0.45;
    }

    .check {
        display: inline-block;
        width: 20px;
        text-align: center;
    }

    .arrow {
        margin-left: auto;
        padding-left: 16px;
    }

    .header {
        padding: 4px 12px 4px 24px;
        opacity: 0.6;
        white-space: nowrap;
    }

    .separator {
        height: 1px;
        margin: 4px 0;
        background: rgba(255, 255, 255, 0.15);
    }

    .submenu {
        position: relative;
    }

    .child {
        display: none;
        position: absolute;
        top: -5px;
        left: 100%;
    }

    .submenu:hover > .child,
    .submenu:focus-within > .child {
        display: block;
    }
</style>
