<script lang="ts">
  import { onMount, tick } from "svelte";
  import ContextMenu from "./components/ContextMenu.svelte";
  import LinkStatus from "./components/LinkStatus.svelte";
  import PluginsTab from "./components/PluginsTab.svelte";
  import ResizeGrips from "./components/ResizeGrips.svelte";
  import SettingsTab from "./components/SettingsTab.svelte";
  import Slider from "./components/Slider.svelte";
  import { fetchCatalog, hello, isInsidePlugin } from "./lib/bridge";
  import { followDisplayScale } from "./lib/display.svelte";
  import {
    editor,
    loadEditorState,
    showTab,
    tabNames,
  } from "./lib/editor.svelte";
  import { controls, listenForChanges, loadCatalog } from "./lib/params.svelte";

  const insidePlugin = isInsidePlugin();
  let status = $state("Checking the bridge…");

  // The tabs share one scrolling area, so each tab's position is kept and put back
  let mainElement: HTMLElement | undefined = $state();
  const scrollTops = new Map<number, number>();

  async function selectTab(index: number) {
    if (mainElement !== undefined)
      scrollTops.set(editor.tab, mainElement.scrollTop);

    showTab(index);
    await tick();

    if (mainElement !== undefined)
      mainElement.scrollTop = scrollTops.get(index) ?? 0;
  }

  onMount(async () => {
    if (!insidePlugin) {
      editor.ready = true;
      status = "Bridge unavailable (not running inside the plugin)";
      return;
    }

    void followDisplayScale();
    void loadEditorState();

    try {
      status = `Bridge connected: ${await hello()}`;
    } catch {
      status = "Bridge did not reply";
      return;
    }

    listenForChanges();

    try {
      if (!loadCatalog(await fetchCatalog()))
        status = "Unexpected parameter list";
    } catch {
      status = "Parameter list did not arrive";
    }
  });
</script>

<div class="editor">
  <div class="tab-bar" role="tablist">
    {#each tabNames as name, index (name)}
      <button
        type="button"
        role="tab"
        aria-selected={editor.tab === index}
        class:selected={editor.tab === index}
        onclick={() => selectTab(index)}>{name}</button
      >
    {/each}
  </div>

  <main bind:this={mainElement}>
    {#if editor.ready}
      {#if editor.tab === 1}
        <PluginsTab />
      {:else if editor.tab === 2}
        <SettingsTab />
      {:else}
        <h1>heartseeker panel</h1>
        <p id="bridge-status">{status}</p>
        {#if editor.processing !== null}
          <p>
            Audio: {editor.processing
              ? "running"
              : "not running (the host has stopped calling this plugin)"}
          </p>
        {/if}

        {#each controls.params as param (param.h)}
          <Slider {param} />
        {/each}

        <LinkStatus />
      {/if}
    {/if}
  </main>
</div>

<ContextMenu />

{#if insidePlugin}
  <ResizeGrips />
{/if}

<style>
  .editor {
    position: fixed;
    inset: 0;
    display: flex;
    flex-direction: column;
  }

  /* Leaves room for the top corner grips */
  .tab-bar {
    display: flex;
    gap: 2px;
    padding: 0 20px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.15);
  }

  .tab-bar button {
    padding: 6px 14px;
    border: none;
    border-bottom: 2px solid transparent;
    background: none;
    color: inherit;
    font: inherit;
    opacity: 0.7;
  }

  .tab-bar button:hover {
    opacity: 1;
  }

  .tab-bar button.selected {
    border-bottom-color: #8fa2ff;
    opacity: 1;
  }

  main {
    flex: 1;
    overflow-y: auto;
    padding: 12px 16px;
  }

  h1 {
    margin: 0 0 8px;
    font-size: 20px;
    font-weight: 500;
  }

  p {
    margin: 0;
  }
</style>
