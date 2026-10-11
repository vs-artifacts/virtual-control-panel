<script lang="ts">
  import { onMount } from "svelte";
  import ContextMenu from "./components/ContextMenu.svelte";
  import HostDiagnostics from "./components/HostDiagnostics.svelte";
  import LinkStatus from "./components/LinkStatus.svelte";
  import PanelTab from "./components/PanelTab.svelte";
  import ResizeGrips from "./components/ResizeGrips.svelte";
  import SettingsTab from "./components/SettingsTab.svelte";
  import {
    fetchCatalog,
    hello,
    isInsidePlugin,
    onStructure,
  } from "./lib/bridge";
  import { followDisplayScale, setRendering } from "./lib/display.svelte";
  import {
    editor,
    loadEditorState,
    readStructure,
    showDiagnostics,
    showTab,
    tabNames,
  } from "./lib/editor.svelte";
  import { columnFlow, scrollMemory } from "./lib/layout";
  import { listenForChanges, loadCatalog } from "./lib/params.svelte";

  const insidePlugin = isInsidePlugin();
  let status = $state("Checking the bridge…");

  $effect(() => setRendering(editor.settings.resizeRendering));

  async function reloadCatalog() {
    try {
      if (!loadCatalog(await fetchCatalog()))
        status = "Unexpected parameter list";
    } catch {
      status = "Parameter list did not arrive";
    }
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

    // A change of the structure also changes the parameter list (handles start again)
    onStructure((raw) => {
      const structure = readStructure(raw);

      if (structure !== null) editor.structure = structure;

      void reloadCatalog();
    });

    await reloadCatalog();
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
    <div class="main">
      <div class="tab">
        {#if editor.ready}
          {#if editor.tab === 1}
            <div class="scroll" use:scrollMemory={"settings"}>
              <div class="flow" use:columnFlow={true}>
                <SettingsTab />
              </div>
            </div>
          {:else}
            <PanelTab {status} />
          {/if}
        {/if}
      </div>
      <!-- Hints for whatever the pointer is over (later) -->
      <div class="hint"></div>
    </div>

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

  /* The tab over the hint pane; Diagnostics, when open, beside them (the window is wider by its width) */
  .main {
    display: flex;
    flex: 1 1 0;
    flex-direction: column;
    min-width: 0;
  }

  .tab {
    flex: 1;
    min-height: 0;
  }

  .hint {
    flex: none;
    height: 40px;
    border-top: 1px solid rgba(255, 255, 255, 0.15);
  }

  /* Each area scrolls on its own. No scroll anchoring: a restored position must stay exactly as it was saved, even
     while late content changes heights above it. */
  .scroll,
  .diagnostics {
    box-sizing: border-box;
    overflow-x: hidden;
    overflow-y: auto;
    overflow-anchor: none;
    padding: 12px 16px;
  }

  .scroll {
    height: 100%;
  }

  .diagnostics {
    flex: none;
    width: 400px;
    border-left: 1px solid rgba(255, 255, 255, 0.15);
  }
</style>
