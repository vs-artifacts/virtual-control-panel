<script lang="ts">
  import { onMount } from "svelte";
  import ContextMenu from "./components/ContextMenu.svelte";
  import ResizeGrips from "./components/ResizeGrips.svelte";
  import Slider from "./components/Slider.svelte";
  import { fetchCatalog, hello, isInsidePlugin } from "./lib/bridge";
  import { followDisplayScale } from "./lib/display";
  import { controls, listenForChanges, loadCatalog } from "./lib/params.svelte";

  const insidePlugin = isInsidePlugin();
  let status = $state("Checking the bridge…");

  onMount(async () => {
    if (!insidePlugin) {
      status = "Bridge unavailable (not running inside the plugin)";
      return;
    }

    void followDisplayScale();

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

<main>
  <h1>heartseeker panel</h1>
  <p id="bridge-status">{status}</p>

  {#each controls.params as param (param.h)}
    <Slider {param} />
  {/each}
</main>

<ContextMenu />

{#if insidePlugin}
  <ResizeGrips />
{/if}

<style>
  main {
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
