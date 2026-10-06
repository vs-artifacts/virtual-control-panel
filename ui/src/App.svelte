<script lang="ts">
  import { onMount } from "svelte";
  import ContextMenu from "./components/ContextMenu.svelte";
  import HostDiagnostics from "./components/HostDiagnostics.svelte";
  import LinkStatus from "./components/LinkStatus.svelte";
  import PluginsTab from "./components/PluginsTab.svelte";
  import ResizeGrips from "./components/ResizeGrips.svelte";
  import SettingsTab from "./components/SettingsTab.svelte";
  import Slider from "./components/Slider.svelte";
  import { fetchCatalog, hello, isInsidePlugin } from "./lib/bridge";
  import { followDisplayScale, setRendering } from "./lib/display.svelte";
  import {
    editor,
    loadEditorState,
    showDiagnostics,
    showTab,
    tabNames,
    tabScrollArea,
  } from "./lib/editor.svelte";
  import { columnFlow, scrollMemory } from "./lib/layout";
  import { controls, listenForChanges, loadCatalog } from "./lib/params.svelte";

  const insidePlugin = isInsidePlugin();
  let status = $state("Checking the bridge…");

  // Beside Diagnostics a tab has half the width, so one column
  const tabArea = $derived(
    tabScrollArea(editor.tab, editor.diagnostics ? "single" : "columns"),
  );

  $effect(() => setRendering(editor.settings.resizeRendering));

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
  <div class="tab-bar">
    <div class="tabs" role="tablist">
      {#each tabNames as name, index (name)}
        <button
          type="button"
          role="tab"
          aria-selected={editor.tab === index}
          class:selected={editor.tab === index}
          onclick={() => showTab(index)}>{name}</button
        >
      {/each}
    </div>
    <button
      type="button"
      class="diagnostics-toggle"
      aria-pressed={editor.diagnostics}
      class:selected={editor.diagnostics}
      onclick={() => showDiagnostics(!editor.diagnostics)}>Diagnostics</button
    >
  </div>

  <div class="body">
    <main use:scrollMemory={tabArea}>
      <div class="flow" use:columnFlow={!editor.diagnostics}>
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
          {/if}
        {/if}
      </div>
    </main>

    {#if editor.diagnostics}
      <aside class="diagnostics" use:scrollMemory={"diagnostics"}>
        <div>
          <LinkStatus />
          <HostDiagnostics />
        </div>
      </aside>
    {/if}
  </div>
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
    justify-content: space-between;
    padding: 0 20px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.15);
  }

  .tabs {
    display: flex;
    gap: 2px;
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

  .body {
    display: flex;
    flex: 1;
    min-height: 0;
  }

  /* Each side scrolls on its own. No scroll anchoring: a restored position must stay exactly as it was saved, even
     while late content changes heights above it. */
  main,
  .diagnostics {
    flex: 1 1 0;
    min-width: 0;
    overflow-x: hidden;
    overflow-y: auto;
    overflow-anchor: none;
    padding: 12px 16px;
  }

  .diagnostics {
    border-left: 1px solid rgba(255, 255, 255, 0.15);
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
