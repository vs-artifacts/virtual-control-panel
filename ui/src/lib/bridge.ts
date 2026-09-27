import { getNativeFunction } from '@juce-framework/webview'

const helloFunction = 'hello'

// Only our plugin's web view registers native functions; a normal browser tab has none
export function isInsidePlugin(): boolean {
    return window.__JUCE__.initialisationData.__juce__functions.includes(helloFunction)
}

export async function hello(timeoutMs = 2000): Promise<string> {
    const reply = await Promise.race([
        getNativeFunction(helloFunction)(),
        new Promise((_, reject) => setTimeout(() => reject(new Error('No reply')), timeoutMs)),
    ])

    if (typeof reply !== 'string') throw new Error('Unexpected reply')

    return reply
}
