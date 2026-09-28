<script lang="ts">
    import { viewportSize } from "../lib/display.svelte";
    import { chooseMenuItem, type MenuItem } from "../lib/menu.svelte";
    import MenuList from "./MenuList.svelte";

    // In page pixels: x is where the left edge goes; if the list doesn't fit there, its right edge goes at flipX
    let {
        items,
        x,
        y,
        flipX,
    }: { items: MenuItem[]; x: number; y: number; flipX: number } = $props();

    // Each list is its own panel on top of the page, kept inside the page; a tall one scrolls
    const maxHeight = viewportSize().height;

    let panel: HTMLElement | undefined = $state();
    let left = $state(0);
    let top = $state(0);
    let submenu: { index: number; x: number; y: number; flipX: number } | null =
        $state(null);

    $effect(() => {
        if (panel === undefined) return;

        const viewport = viewportSize();
        const width = panel.offsetWidth;
        const placed = x + width <= viewport.width ? x : flipX - width;

        left = Math.max(0, Math.min(placed, viewport.width - width));
        top = Math.max(0, Math.min(y, viewport.height - panel.offsetHeight));
    });

    // Next to its item, with its first item level with it (4px padding and 1px border)
    function openSubmenu(index: number, button: HTMLElement) {
        if (panel === undefined) return;

        submenu = {
            index,
            x: left + panel.offsetWidth - 2,
            y: top + button.offsetTop - panel.scrollTop - 5,
            flipX: left + 2,
        };
    }

    function closeSubmenu() {
        submenu = null;
    }
</script>

<div
    class="menu-list"
    role="menu"
    bind:this={panel}
    style:left="{left}px"
    style:top="{top}px"
    style:max-height="{maxHeight}px"
>
    {#each items as item, index (index)}
        {#if item.kind === "separator"}
            <div class="separator" role="separator"></div>
        {:else if item.kind === "header"}
            <div class="header">{item.text}</div>
        {:else if item.kind === "submenu"}
            <button
                type="button"
                role="menuitem"
                aria-haspopup="menu"
                aria-expanded={submenu?.index === index}
                class:open={submenu?.index === index}
                disabled={!item.enabled}
                onpointerenter={(event) =>
                    openSubmenu(index, event.currentTarget)}
                onclick={(event) => openSubmenu(index, event.currentTarget)}
            >
                <span class="check"></span>{item.text}<span class="arrow"
                    >▸</span
                >
            </button>
        {:else}
            <button
                type="button"
                role="menuitem"
                disabled={!item.enabled}
                onpointerenter={closeSubmenu}
                onclick={() => chooseMenuItem(item.id)}
            >
                <span class="check">{item.checked ? "✓" : ""}</span>{item.text}
            </button>
        {/if}
    {/each}
</div>

{#if submenu !== null}
    {@const child = items[submenu.index]}
    {#if child?.kind === "submenu"}
        {#key submenu.index}
            <MenuList
                items={child.items}
                x={submenu.x}
                y={submenu.y}
                flipX={submenu.flipX}
            />
        {/key}
    {/if}
{/if}

<style>
    .menu-list {
        position: fixed;
        z-index: 100;
        box-sizing: border-box;
        overflow-y: auto;
        min-width: 180px;
        padding: 4px 0;
        background: rgba(30, 34, 64, 0.96);
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
    button.open {
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
</style>
