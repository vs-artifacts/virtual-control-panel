import { mount } from 'svelte'
import './app.css'
import App from './App.svelte'
import { blockBrowserBehaviour } from './lib/guards'
import { forwardHostKeys } from './lib/keys'
import { followShortcuts } from './lib/shortcuts.svelte'

blockBrowserBehaviour()
followShortcuts()
forwardHostKeys()

const app = mount(App, {
  target: document.getElementById('app')!,
})

export default app
