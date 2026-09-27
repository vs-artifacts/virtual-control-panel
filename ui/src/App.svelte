<script lang="ts">
  import { onMount } from 'svelte'
  import { hello, isInsidePlugin } from './lib/bridge'

  let status = $state('Checking the bridge…')

  onMount(async () => {
    if (!isInsidePlugin()) {
      status = 'Bridge unavailable (not running inside the plugin)'
      return
    }

    try {
      status = `Bridge connected: ${await hello()}`
    } catch {
      status = 'Bridge did not reply'
    }
  })
</script>

<main>
  <h1>heartseeker panel</h1>
  <p id="bridge-status">{status}</p>
</main>

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
