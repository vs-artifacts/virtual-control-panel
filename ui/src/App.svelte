<script lang="ts">
  import { onMount } from "svelte";
  import ContextMenu from "./components/ContextMenu.svelte";
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
  import { followHints, hint, hinted } from "./lib/hint.svelte";
  import { columnFlow, scrollMemory } from "./lib/layout";
  import { listenForChanges, loadCatalog } from "./lib/params.svelte";
  import {
    applyPixelSizes,
    followPixelText,
    setSnapping,
  } from "./lib/pixelText.svelte";

  const insidePlugin = isInsidePlugin();
  let status = $state("Checking the bridge…");

  $effect(() => setRendering(editor.settings.resizeRendering));

  // The page's fonts (see app.css): pixel text with or without anti-aliasing, or Barlow for everything
  $effect(() => {
    const root = document.documentElement.classList;
    root.toggle("pixel-antialias", editor.settings.pixelTextAntialiasing);
    root.toggle("font-fallback", editor.settings.fontFallback);
  });

  $effect(() =>
    applyPixelSizes(
      editor.settings.pixelTextAntialiasing,
      editor.settings.pixelStepAtPercent,
      editor.settings.pixelStepAntialiasedPercent,
    ),
  );

  $effect(() =>
    setSnapping(editor.settings.pixelTextSnap && !editor.settings.fontFallback),
  );

  followHints();
  followPixelText();

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
      use:hinted={"Shows the diagnostics beside the tabs; the window widens by 400 px."}
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
      {#key hint.text}
        <div class="hint" id="hint">{hint.text}</div>
      {/key}
    </div>

    {#if editor.diagnostics}
      <aside class="diagnostics" use:scrollMemory={"diagnostics"}>
        <div>
          <LinkStatus />
        </div>
      </aside>
    {/if}
  </div>
</div>

<!-- Pixels at least 75% covered on, the rest off: pixel fonts without anti-aliasing (the .crisp class). Windows smears
     strokes sideways, so a 1-pixel gap between two strokes reaches about 60% and must stay off. -->
<svg class="filters" aria-hidden="true">
  <filter id="pixel-text" color-interpolation-filters="sRGB">
    <feComponentTransfer>
      <feFuncA type="discrete" tableValues="0 0 0 1" />
    </feComponentTransfer>
  </filter>
</svg>

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
    border-bottom-color: var(--accent);
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

  /* About 2.5 lines; longer hints scroll */
  .hint {
    flex: none;
    box-sizing: border-box;
    height: 60px;
    padding: 4px 16px;
    overflow-y: auto;
    border-top: 1px solid rgba(255, 255, 255, 0.15);
    color: var(--mid-gray);
    white-space: pre-line;
  }

  /* Takes no room; display: none would switch the filter off */
  .filters {
    position: absolute;
    width: 0;
    height: 0;
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
