<script lang="ts">
    import { currentZoom } from "../lib/display";
    import { closeMenu, menu } from "../lib/menu.svelte";
    import MenuList from "./MenuList.svelte";

    let element: HTMLElement | undefined = $state();
    let left = $state(0);
    let top = $state(0);

    // Keeps the menu inside the page
    $effect(() => {
        if (!menu.open || element === undefined || menu.items.length === 0)
            return;

        const zoom = currentZoom();
        left = Math.max(
            0,
            Math.min(menu.x, window.innerWidth / zoom - element.offsetWidth),
        );
        top = Math.max(
            0,
            Math.min(menu.y, window.innerHeight / zoom - element.offsetHeight),
        );
    });

    function dismissOutside(event: PointerEvent) {
        if (
            menu.open &&
            element !== undefined &&
            !element.contains(event.target as Node)
        )
            closeMenu();
    }
</script>

<svelte:window
    onpointerdown={dismissOutside}
    onkeydown={(event) => {
        if (event.key === "Escape") closeMenu();
    }}
/>

{#if menu.open}
    <div
        class="context-menu"
        bind:this={element}
        style:left="{left}px"
        style:top="{top}px"
    >
        <MenuList items={menu.items} />
    </div>
{/if}

<style>
    .context-menu {
        position: fixed;
        z-index: 100;
    }
</style>
