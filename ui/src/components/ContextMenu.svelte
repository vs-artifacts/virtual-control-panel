<script lang="ts">
    import { closeMenu, menu } from "../lib/menu.svelte";
    import MenuList from "./MenuList.svelte";

    let element: HTMLElement | undefined = $state();

    function dismissOutside(event: PointerEvent) {
        if (
            menu.open &&
            element !== undefined &&
            !element.contains(event.target as Node)
        )
            closeMenu();
    }
</script>

<!-- Clicking elsewhere (in the page or outside it), Escape, or a size change closes the menu -->
<svelte:window
    onpointerdown={dismissOutside}
    onblur={closeMenu}
    onresize={closeMenu}
    onkeydown={(event) => {
        if (event.key === "Escape") closeMenu();
    }}
/>

{#if menu.open}
    <div class="context-menu" bind:this={element}>
        <MenuList items={menu.items} x={menu.x} y={menu.y} flipX={menu.x} />
    </div>
{/if}
