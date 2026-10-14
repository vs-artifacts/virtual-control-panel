<script lang="ts">
    import {
        sendRecheckBlockSize,
        sendRetryOneBlock,
        type ResetKind,
    } from "../lib/bridge";
    import { view } from "../lib/display.svelte";
    import {
        centreModifier,
        changeSetting,
        doubleClickRange,
        editor,
        fadeSettings,
        lowestTempoRange,
        pixelStepAntialiasedRange,
        pixelStepAtRange,
        quietLevelRange,
        quietMsRange,
        releaseRange,
        resetSettings,
        resetSize,
        settleRange,
        slotChangeFadeSettings,
        swapSize,
        type FadeKey,
        type SettingKey,
        type Settings,
    } from "../lib/editor.svelte";
    import { hinted } from "../lib/hint.svelte";
    import { link } from "../lib/link.svelte";
    import { openPageMenu } from "../lib/menu.svelte";
    import {
        openGroupMenu,
        openSettingMenu,
        typedNumber,
    } from "../lib/settingMenu";

    // A reset waiting for its confirmation
    let confirming: ResetKind | null = $state(null);

    const confirmations: Record<
        ResetKind,
        { question: string; button: string }
    > = {
        defaults: {
            question: "Reset every setting to its default?",
            button: "Reset all",
        },
        factory: {
            question:
                "Set every setting to its factory default? Your own defaults are kept.",
            button: "Factory reset",
        },
        clearUserDefaults: {
            question: "Forget all your own defaults? The current values stay.",
            button: "Clear user defaults",
        },
        hard: {
            question:
                "Clear every setting and all your own defaults, back to the factory defaults?",
            button: "Hard reset",
        },
    };

    // A button whose right-click holds another option
    function offerOnRightClick(
        event: MouseEvent,
        text: string,
        hint: string,
        kind: ResetKind,
    ) {
        event.preventDefault();
        openPageMenu(
            [
                {
                    text,
                    enabled: true,
                    hint,
                    action: () => (confirming = kind),
                },
            ],
            event.clientX,
            event.clientY,
        );
    }

    // Each group's settings, for its heading's right-click menu
    const groups = {
        editorPlacement: ["editorPlacement"],
        attachedPlacement: ["attachedPlacement"],
        resizing: ["resizeRendering", "resizeFlipAlt"],
        slotText: [
            "pixelTextAntialiasing",
            "fontFallback",
            "pixelTextSnap",
            "pixelStepAtPercent",
            "pixelStepAntialiasedPercent",
        ],
        doubleClick: ["doubleClickMs", "doubleClickSets"],
        roundTrip: ["bayRoundTrip"],
        countedIn: ["bayRealBlocks", "bayLowestTempo"],
        stoppedShrink: ["bayStoppedShrink", "bayQuietLevelDb", "bayQuietMs"],
        bayFades: fadeSettings.map((fade) => fade.key),
        slotChanges: slotChangeFadeSettings.map((fade) => fade.key),
        settle: ["bayLatencySettleMs"],
        tailCutRelease: ["bayTailCutReleaseMs"],
    } satisfies Record<string, SettingKey[]>;

    const placements: { value: Settings["editorPlacement"]; text: string }[] = [
        { value: "off", text: "Off" },
        { value: "hostPlaced", text: "The monitor the host opened it on" },
        { value: "mouse", text: "The monitor under the mouse" },
        {
            value: "hostMainWindow",
            text: "The monitor with the host's main window",
        },
    ];

    const attachedPlacements: {
        value: Settings["attachedPlacement"];
        text: string;
        hint: string;
    }[] = [
        { value: "off", text: "Off", hint: "" },
        { value: "workspace", text: "Centered in FL's workspace", hint: "" },
        {
            value: "hostMonitor",
            text: "Centered on the monitor where FL is open",
            hint: "Kept inside the workspace.",
        },
    ];

    const renderings: {
        value: Settings["resizeRendering"];
        text: string;
        hint: string;
    }[] = [
        {
            value: "smooth",
            text: "Smooth",
            hint: "Scales like a picture: everything keeps its proportions, text included.\nSoft while resizing, sharp again when it stops.",
        },
        {
            value: "sharp",
            text: "Sharp",
            hint: "Laid out again at every size: crisp, but elements can shift by a pixel.",
        },
    ];

    const roundTrips: {
        value: Settings["bayRoundTrip"];
        text: string;
        hint: string;
    }[] = [
        {
            value: 0,
            text: "Automatic",
            hint: "1 block if the receiver runs after the panel, otherwise 2.\nChecked when relays link.",
        },
        {
            value: 1,
            text: "1 block",
            hint: "The receiver must run after the panel.",
        },
        { value: 2, text: "2 blocks", hint: "" },
        { value: 3, text: "3 blocks", hint: "" },
    ];

    const stoppedShrinks: {
        value: Settings["bayStoppedShrink"];
        text: string;
        hint: string;
    }[] = [
        { value: "never", text: "Never", hint: "" },
        {
            value: "whenQuiet",
            text: "Only when the panel's output is quiet too",
            hint: "Quiet: as set below.",
        },
        { value: "always", text: "Always", hint: "" },
    ];

    const resizeHint = $derived(
        `Corner grips: drag keeps the opposite corner in place, ${centreModifier}+drag the window's center (separate windows, and windows attached inside FL)${editor.settings.resizeFlipAlt ? "; flipped by the setting below" : ""}. ` +
            "Add Ctrl for 25% steps. Shift sets width and height apart: the page keeps its scale and shows more or less (with Ctrl, each in 25% steps of the design size).",
    );

    const swapHint = $derived(
        editor.settings.resizeFlipAlt
            ? `Swaps the window's width and height around its center (Diagnostics' extra width stays out of it). ${centreModifier}+click keeps its top-left corner in place.`
            : `Swaps the window's width and height, keeping its top-left corner in place (Diagnostics' extra width stays out of it). ${centreModifier}+click swaps around its center.`,
    );

    const roundTripHint =
        'The host is told it as latency. The host can automate both buttons: "Bay 1: retry 1-block round trip" and "Bays: recheck block size" (each acts when switched on).';

    const fadesHint = "Milliseconds; 0 is off.";

    // At 2 or 3 blocks each change of the real block size makes the receiver line up again: a gap inside the bay
    const tempoDropsPop = $derived(
        editor.settings.bayRealBlocks &&
            editor.settings.bayLowestTempo === 0 &&
            (link.status?.roundTripBlocks ?? 0) > 1,
    );

    function sentence(text: string): string {
        return `${text.charAt(0).toUpperCase()}${text.slice(1)}.`;
    }

    function range(limits: { min: number; max: number }): string {
        return `(${limits.min} to ${limits.max})`;
    }

    // Whole numbers kept in range; anything else puts the field back
    function changeWhole(
        event: Event & { currentTarget: HTMLInputElement },
        name:
            | "bayQuietLevelDb"
            | "bayQuietMs"
            | "bayLatencySettleMs"
            | "bayTailCutReleaseMs"
            | "doubleClickMs"
            | "pixelStepAtPercent"
            | "pixelStepAntialiasedPercent"
            | FadeKey,
        limits: { min: number; max: number },
    ) {
        const typed = event.currentTarget.valueAsNumber;

        if (Number.isFinite(typed))
            changeSetting(
                name,
                Math.min(limits.max, Math.max(limits.min, Math.round(typed))),
            );

        event.currentTarget.value = String(editor.settings[name]);
    }

    // An empty field is no floor (0); anything else is kept in range, to a tenth of a BPM
    function changeLowestTempo(
        event: Event & { currentTarget: HTMLInputElement },
    ) {
        const field = event.currentTarget;
        const typed = field.value.trim() === "" ? 0 : field.valueAsNumber;

        if (Number.isFinite(typed)) {
            const bpm = Math.min(
                lowestTempoRange.max,
                Math.max(lowestTempoRange.min, Math.round(typed * 10) / 10),
            );
            changeSetting("bayLowestTempo", bpm);
        }

        field.value =
            editor.settings.bayLowestTempo > 0
                ? String(editor.settings.bayLowestTempo)
                : "";
    }
</script>

<p class="version">{editor.title}</p>

<div class="line" id="reset-all">
    {#if confirming !== null}
        {@const kind = confirming}
        <span>{confirmations[kind].question}</span>
        <button
            type="button"
            id="confirm-reset-all"
            onclick={() => {
                resetSettings(kind);
                confirming = null;
            }}>{confirmations[kind].button}</button
        >
        <button type="button" onclick={() => (confirming = null)}>Cancel</button
        >
    {:else}
        <button
            type="button"
            id="reset-all-settings"
            class="more"
            use:hinted={"Every setting back to its default: your own where you've set one, else the factory's.\nRight-click: clear user defaults."}
            onclick={() => (confirming = "defaults")}
            oncontextmenu={(event) =>
                offerOnRightClick(
                    event,
                    "Clear user defaults",
                    "Retains current option values.",
                    "clearUserDefaults",
                )}>Reset all</button
        >
        <button
            type="button"
            id="factory-reset-settings"
            class="more"
            use:hinted={"Every setting to its factory default; your own defaults are kept.\nRight-click: hard reset to factory defaults."}
            onclick={() => (confirming = "factory")}
            oncontextmenu={(event) =>
                offerOnRightClick(
                    event,
                    "Hard reset to factory defaults",
                    "Clears all settings options, user defaults, and resets values to factory defaults.",
                    "hard",
                )}>Factory reset</button
        >
    {/if}
</div>
<p class="note">
    Right-click any setting to make its value your default, or to reset it;
    right-click a heading to reset its whole group.
</p>

<div
    class="option"
    role="group"
    use:hinted={"Reopens instantly, uses more memory."}
    oncontextmenu={(event) =>
        openSettingMenu(
            event,
            "keepWebViewWarm",
            editor.settings.keepWebViewWarm,
        )}
>
    <input
        type="checkbox"
        aria-label="Keep web UI warm"
        checked={editor.settings.keepWebViewWarm}
        onchange={(event) =>
            changeSetting("keepWebViewWarm", event.currentTarget.checked)}
    />
    <span>Keep web UI warm</span>
</div>

<div
    class="option"
    role="group"
    use:hinted={"FL Studio multiplies a generator's added outputs; this undoes it (x1.283291). We have no control outputs yet: a safety setting for when we do."}
    oncontextmenu={(event) =>
        openSettingMenu(
            event,
            "flOutputCorrection",
            editor.settings.flOutputCorrection,
        )}
>
    <input
        type="checkbox"
        aria-label="Correct FL Studio's gain on our control outputs"
        checked={editor.settings.flOutputCorrection}
        onchange={(event) =>
            changeSetting("flOutputCorrection", event.currentTarget.checked)}
    />
    <span>Correct FL Studio's gain on our control outputs (FL only)</span>
</div>

<div
    class="option"
    role="group"
    use:hinted={"Releasing Shift while Alt or Ctrl is held switches Windows' input language unless another key came in between: the panel types one meaningless key."}
    oncontextmenu={(event) =>
        openSettingMenu(
            event,
            "resizeBlocksLanguageSwitch",
            editor.settings.resizeBlocksLanguageSwitch,
        )}
>
    <input
        type="checkbox"
        aria-label="While a window corner is dragged, stop Shift with Alt or Ctrl from switching Windows' input language"
        checked={editor.settings.resizeBlocksLanguageSwitch}
        onchange={(event) =>
            changeSetting(
                "resizeBlocksLanguageSwitch",
                event.currentTarget.checked,
            )}
    />
    <span
        >While a window corner is dragged, stop Shift with Alt or Ctrl from
        switching Windows' input language (Windows only)</span
    >
</div>

<div class="group" id="window-settings">
    <fieldset>
        <legend
            class="h2"
            use:hinted={"Instances already placed aren't centered again."}
            oncontextmenu={(event) =>
                openGroupMenu(event, groups.editorPlacement)}
            >Center a separate window when it first opens on</legend
        >
        {#each placements as option (option.value)}
            <div
                class="option"
                role="group"
                oncontextmenu={(event) =>
                    openSettingMenu(event, "editorPlacement", option.value)}
            >
                <input
                    type="radio"
                    name="editorPlacement"
                    aria-label={option.text}
                    checked={editor.settings.editorPlacement === option.value}
                    onchange={() =>
                        changeSetting("editorPlacement", option.value)}
                />
                <span>{option.text}</span>
            </div>
        {/each}
    </fieldset>

    <fieldset>
        <legend
            class="h2"
            use:hinted={"Instances already placed aren't centered again."}
            oncontextmenu={(event) =>
                openGroupMenu(event, groups.attachedPlacement)}
            >Center a window attached inside FL Studio when it first opens</legend
        >
        {#each attachedPlacements as option (option.value)}
            <div
                class="option"
                role="group"
                use:hinted={option.hint}
                oncontextmenu={(event) =>
                    openSettingMenu(event, "attachedPlacement", option.value)}
            >
                <input
                    type="radio"
                    name="attachedPlacement"
                    aria-label={option.text}
                    checked={editor.settings.attachedPlacement === option.value}
                    onchange={() =>
                        changeSetting("attachedPlacement", option.value)}
                />
                <span>{option.text}</span>
            </div>
        {/each}
    </fieldset>

    <fieldset>
        <legend
            class="h2"
            use:hinted={resizeHint}
            oncontextmenu={(event) => openGroupMenu(event, groups.resizing)}
            >Resizing</legend
        >
        {#each renderings as option (option.value)}
            <div
                class="option"
                role="group"
                use:hinted={option.hint}
                oncontextmenu={(event) =>
                    openSettingMenu(event, "resizeRendering", option.value)}
            >
                <input
                    type="radio"
                    name="resizeRendering"
                    aria-label={option.text}
                    checked={editor.settings.resizeRendering === option.value}
                    onchange={() =>
                        changeSetting("resizeRendering", option.value)}
                />
                <span>{option.text}</span>
            </div>
        {/each}
        <div
            class="option"
            role="group"
            use:hinted={`On: drag resizes around the window's center, and ${centreModifier}+drag keeps the opposite corner in place. Swap follows it too.`}
            oncontextmenu={(event) =>
                openSettingMenu(
                    event,
                    "resizeFlipAlt",
                    editor.settings.resizeFlipAlt,
                )}
        >
            <input
                type="checkbox"
                aria-label="Window resize: flip drag and alt+drag"
                checked={editor.settings.resizeFlipAlt}
                onchange={(event) =>
                    changeSetting("resizeFlipAlt", event.currentTarget.checked)}
            />
            <span>Window resize: flip drag and alt+drag</span>
        </div>
    </fieldset>

    <fieldset id="text-settings">
        <legend
            class="h2"
            use:hinted={"The slot fonts (m5x7, Tiny5) are pixel fonts: each font pixel is kept a whole number of screen pixels, stepping up as the window grows."}
            oncontextmenu={(event) => openGroupMenu(event, groups.slotText)}
            >Slot text</legend
        >
        <div
            class="option"
            role="group"
            use:hinted={"Smooths the edges of the pixel fonts, which then grow in smaller steps (below)."}
            oncontextmenu={(event) =>
                openSettingMenu(
                    event,
                    "pixelTextAntialiasing",
                    editor.settings.pixelTextAntialiasing,
                )}
        >
            <input
                type="checkbox"
                aria-label="Anti-alias pixel text"
                disabled={editor.settings.fontFallback}
                checked={editor.settings.pixelTextAntialiasing}
                onchange={(event) =>
                    changeSetting(
                        "pixelTextAntialiasing",
                        event.currentTarget.checked,
                    )}
            />
            <span>Anti-alias pixel text</span>
        </div>
        <div
            class="option"
            role="group"
            use:hinted={"Moves each piece of pixel text by a fraction of a pixel so it starts on a whole screen pixel. Off: strokes can blur or vanish at sizes that aren't whole multiples."}
            oncontextmenu={(event) =>
                openSettingMenu(
                    event,
                    "pixelTextSnap",
                    editor.settings.pixelTextSnap,
                )}
        >
            <input
                type="checkbox"
                aria-label="Snap pixel text to whole screen pixels"
                disabled={editor.settings.fontFallback}
                checked={editor.settings.pixelTextSnap}
                onchange={(event) =>
                    changeSetting("pixelTextSnap", event.currentTarget.checked)}
            />
            <span>Snap pixel text to whole screen pixels</span>
        </div>
        <div
            class="option"
            role="group"
            use:hinted={`Where the next size starts, as a share of the way to the next whole scale: 50 steps up at 150%, 250% and so on ${range(pixelStepAtRange)}.`}
            oncontextmenu={(event) =>
                openSettingMenu(
                    event,
                    "pixelStepAtPercent",
                    typedNumber(
                        event.currentTarget.querySelector("input"),
                        "pixelStepAtPercent",
                    ),
                )}
        >
            <input
                id="pixel-step-at"
                class="number"
                type="number"
                aria-label="Next pixel size from this percentage past each step"
                disabled={editor.settings.fontFallback}
                min={pixelStepAtRange.min}
                max={pixelStepAtRange.max}
                step="1"
                value={editor.settings.pixelStepAtPercent}
                onchange={(event) =>
                    changeWhole(event, "pixelStepAtPercent", pixelStepAtRange)}
            />
            <span>% past each step: next pixel size</span>
        </div>
        <div
            class="option"
            role="group"
            use:hinted={`Anti-aliased pixel text grows in steps of this share of a screen pixel per font pixel ${range(pixelStepAntialiasedRange)}.`}
            oncontextmenu={(event) =>
                openSettingMenu(
                    event,
                    "pixelStepAntialiasedPercent",
                    typedNumber(
                        event.currentTarget.querySelector("input"),
                        "pixelStepAntialiasedPercent",
                    ),
                )}
        >
            <input
                id="pixel-step-antialiased"
                class="number"
                type="number"
                aria-label="Anti-aliased step in percent of a screen pixel"
                disabled={editor.settings.fontFallback}
                min={pixelStepAntialiasedRange.min}
                max={pixelStepAntialiasedRange.max}
                step="1"
                value={editor.settings.pixelStepAntialiasedPercent}
                onchange={(event) =>
                    changeWhole(
                        event,
                        "pixelStepAntialiasedPercent",
                        pixelStepAntialiasedRange,
                    )}
            />
            <span>% of a pixel: anti-aliased step</span>
        </div>
        <div
            class="option"
            role="group"
            use:hinted={"All text in Barlow: slot text at 10 px, small slot text at 8 px, always anti-aliased."}
            oncontextmenu={(event) =>
                openSettingMenu(
                    event,
                    "fontFallback",
                    editor.settings.fontFallback,
                )}
        >
            <input
                type="checkbox"
                aria-label="Rendering fallback: Barlow instead of the pixel fonts"
                checked={editor.settings.fontFallback}
                onchange={(event) =>
                    changeSetting("fontFallback", event.currentTarget.checked)}
            />
            <span>Rendering fallback: Barlow instead of the pixel fonts</span>
        </div>
    </fieldset>

    <div class="size">
        <span
            >Size: {view.width} x {view.height} ({Math.round(
                view.uiScale * 100,
            )}%)</span
        >
        <span class="buttons">
            <button
                type="button"
                id="swap-size"
                use:hinted={swapHint}
                onclick={swapSize}>Swap width and height</button
            >
            <button
                type="button"
                use:hinted={"Back to the design size (790 x 1270) at 100%."}
                onclick={resetSize}>Reset size</button
            >
        </span>
    </div>
</div>

<fieldset>
    <legend
        class="h2"
        use:hinted={"Two clicks within this time (and 6 page pixels) make a double-click, which resets a control to its default."}
        oncontextmenu={(event) => openGroupMenu(event, groups.doubleClick)}
        >Double-click on a control</legend
    >
    <div
        class="option"
        role="group"
        use:hinted={range(doubleClickRange)}
        oncontextmenu={(event) =>
            openSettingMenu(
                event,
                "doubleClickMs",
                typedNumber(
                    event.currentTarget.querySelector("input"),
                    "doubleClickMs",
                ),
            )}
    >
        <input
            id="double-click-ms"
            class="number"
            type="number"
            aria-label="Milliseconds at most between the clicks"
            min={doubleClickRange.min}
            max={doubleClickRange.max}
            step="1"
            value={editor.settings.doubleClickMs}
            onchange={(event) =>
                changeWhole(event, "doubleClickMs", doubleClickRange)}
        />
        <span>ms at most between the clicks</span>
    </div>
    <div
        class="option"
        role="group"
        use:hinted={"Not built yet: still resets."}
        oncontextmenu={(event) =>
            openSettingMenu(
                event,
                "doubleClickSets",
                editor.settings.doubleClickSets,
            )}
    >
        <input
            type="checkbox"
            aria-label="Double-click sets a value instead of resetting to the default"
            checked={editor.settings.doubleClickSets}
            onchange={(event) =>
                changeSetting("doubleClickSets", event.currentTarget.checked)}
        />
        <span
            >Double-click sets a value instead of resetting to the default</span
        >
    </div>
</fieldset>

<div class="group" id="bay-settings">
    <fieldset>
        <legend
            class="h2"
            use:hinted={roundTripHint}
            oncontextmenu={(event) => openGroupMenu(event, groups.roundTrip)}
            >Bay round trip</legend
        >
        {#each roundTrips as option (option.value)}
            <div class="line">
                <div
                    class="option"
                    role="group"
                    use:hinted={option.hint}
                    oncontextmenu={(event) =>
                        openSettingMenu(event, "bayRoundTrip", option.value)}
                >
                    <input
                        type="radio"
                        name="bayRoundTrip"
                        aria-label={option.text}
                        checked={editor.settings.bayRoundTrip === option.value}
                        onchange={() =>
                            changeSetting("bayRoundTrip", option.value)}
                    />
                    <span>{option.text}</span>
                </div>
                {#if option.value === 0}
                    <button
                        type="button"
                        id="retry-one-block"
                        disabled={editor.settings.bayRoundTrip !== 0}
                        use:hinted={'Automatic only: tries a 1-block round trip again. The host can automate it: "Bay 1: retry 1-block round trip".'}
                        onclick={sendRetryOneBlock}>Retry 1 block</button
                    >
                {/if}
            </div>
        {/each}
        <div class="line">
            <button
                type="button"
                id="recheck-block-size"
                disabled={!editor.settings.bayRealBlocks}
                use:hinted={'Real blocks only: watches the blocks afresh, so the round trip can shrink as well as grow. The host can automate it: "Bays: recheck block size".'}
                onclick={sendRecheckBlockSize}>Recheck real block size</button
            >
        </div>
    </fieldset>

    <fieldset>
        <legend
            class="h2"
            oncontextmenu={(event) => openGroupMenu(event, groups.countedIn)}
            >Bay round trip counted in</legend
        >
        <div
            class="option"
            role="group"
            use:hinted={"FL: one sequencer tick each, far below the buffer length.\nGrown ahead of need, 25% to spare."}
            oncontextmenu={(event) =>
                openSettingMenu(event, "bayRealBlocks", true)}
        >
            <input
                type="radio"
                name="bayRealBlocks"
                aria-label="Real blocks"
                checked={editor.settings.bayRealBlocks}
                onchange={() => changeSetting("bayRealBlocks", true)}
            />
            <span>Real blocks</span>
        </div>
        <div
            class="option"
            role="group"
            use:hinted={"The host's buffer length."}
            oncontextmenu={(event) =>
                openSettingMenu(event, "bayRealBlocks", false)}
        >
            <input
                type="radio"
                name="bayRealBlocks"
                aria-label="Announced blocks"
                checked={!editor.settings.bayRealBlocks}
                onchange={() => changeSetting("bayRealBlocks", false)}
            />
            <span>Announced blocks</span>
        </div>
        <div
            class="fields"
            role="group"
            use:hinted={`Empty: none. Real blocks start at one tick at the lowest tempo, so a project that slows down later needn't grow the round trip while playing. Setting it rechecks the block size. (1 to ${lowestTempoRange.max})`}
            oncontextmenu={(event) =>
                openSettingMenu(
                    event,
                    "bayLowestTempo",
                    typedNumber(
                        event.currentTarget.querySelector("input"),
                        "bayLowestTempo",
                    ),
                )}
        >
            <span>Lowest tempo</span>
            <input
                id="lowest-tempo"
                class="number"
                type="number"
                aria-label="Lowest tempo in BPM"
                min="1"
                max={lowestTempoRange.max}
                step="any"
                placeholder="none"
                value={editor.settings.bayLowestTempo > 0
                    ? editor.settings.bayLowestTempo
                    : ""}
                onchange={changeLowestTempo}
            />
            <span>BPM</span>
        </div>
        {#if tempoDropsPop}
            <p class="warning" id="tempo-drop-warning">
                At 2 or 3 blocks, tempo drops can pop inside the bay (each
                growth of the round trip makes the receiver line up again). Set
                a lowest tempo to avoid it.
            </p>
        {/if}
    </fieldset>

    <fieldset>
        <legend
            class="h2"
            use:hinted={"While stopped, the block size is checked once (as soon as the rule allows), and again only if the host's tempo changes, or a check is asked for. Playing, the round trip only grows."}
            oncontextmenu={(event) =>
                openGroupMenu(event, groups.stoppedShrink)}
            >Real blocks: shrink the round trip while the transport is stopped</legend
        >
        {#each stoppedShrinks as option (option.value)}
            <div
                class="option"
                role="group"
                use:hinted={option.hint}
                oncontextmenu={(event) =>
                    openSettingMenu(event, "bayStoppedShrink", option.value)}
            >
                <input
                    type="radio"
                    name="bayStoppedShrink"
                    aria-label={option.text}
                    checked={editor.settings.bayStoppedShrink === option.value}
                    onchange={() =>
                        changeSetting("bayStoppedShrink", option.value)}
                />
                <span>{option.text}</span>
            </div>
        {/each}
        <div
            class="fields"
            use:hinted={`Level ${range(quietLevelRange)}, time ${range(quietMsRange)}.`}
        >
            <span>Quiet: below</span>
            <input
                id="quiet-level"
                class="number"
                type="number"
                aria-label="Quiet level in dBFS"
                min={quietLevelRange.min}
                max={quietLevelRange.max}
                step="1"
                value={editor.settings.bayQuietLevelDb}
                onchange={(event) =>
                    changeWhole(event, "bayQuietLevelDb", quietLevelRange)}
                oncontextmenu={(event) =>
                    openSettingMenu(
                        event,
                        "bayQuietLevelDb",
                        typedNumber(event.currentTarget, "bayQuietLevelDb"),
                    )}
            />
            <span>dBFS for</span>
            <input
                id="quiet-ms"
                class="number"
                type="number"
                aria-label="Quiet time in milliseconds"
                min={quietMsRange.min}
                max={quietMsRange.max}
                step="1"
                value={editor.settings.bayQuietMs}
                onchange={(event) =>
                    changeWhole(event, "bayQuietMs", quietMsRange)}
                oncontextmenu={(event) =>
                    openSettingMenu(
                        event,
                        "bayQuietMs",
                        typedNumber(event.currentTarget, "bayQuietMs"),
                    )}
            />
            <span>ms</span>
        </div>
    </fieldset>

    <fieldset id="bay-fades">
        <legend
            class="h2"
            use:hinted={fadesHint}
            oncontextmenu={(event) => openGroupMenu(event, groups.bayFades)}
            >Bay fades</legend
        >
        {#each fadeSettings as fade (fade.key)}
            <div
                class="option"
                role="group"
                use:hinted={`${sentence(fade.covers)} (0 to ${fade.max})`}
                oncontextmenu={(event) =>
                    openSettingMenu(
                        event,
                        fade.key,
                        typedNumber(
                            event.currentTarget.querySelector("input"),
                            fade.key,
                        ),
                    )}
            >
                <input
                    class="number"
                    type="number"
                    aria-label="{fade.label} in milliseconds"
                    min="0"
                    max={fade.max}
                    step="1"
                    value={editor.settings[fade.key]}
                    onchange={(event) =>
                        changeWhole(event, fade.key, { min: 0, max: fade.max })}
                />
                <span>{fade.label}</span>
            </div>
        {/each}
    </fieldset>

    <fieldset id="slot-change-fades">
        <legend
            class="h2"
            use:hinted={fadesHint}
            oncontextmenu={(event) => openGroupMenu(event, groups.slotChanges)}
            >Slot changes</legend
        >
        {#each slotChangeFadeSettings as fade (fade.key)}
            <div
                class="option"
                role="group"
                use:hinted={`${sentence(fade.covers)} (0 to ${fade.max})`}
                oncontextmenu={(event) =>
                    openSettingMenu(
                        event,
                        fade.key,
                        typedNumber(
                            event.currentTarget.querySelector("input"),
                            fade.key,
                        ),
                    )}
            >
                <input
                    class="number"
                    type="number"
                    aria-label="{fade.label} in milliseconds"
                    min="0"
                    max={fade.max}
                    step="1"
                    value={editor.settings[fade.key]}
                    onchange={(event) =>
                        changeWhole(event, fade.key, { min: 0, max: fade.max })}
                />
                <span>{fade.label}</span>
            </div>
        {/each}
    </fieldset>

    <fieldset>
        <legend
            class="h2"
            use:hinted={"A new bay latency must hold this long before the panel uses it; meanwhile the bay's sound is held back (silence, faded)."}
            oncontextmenu={(event) => openGroupMenu(event, groups.settle)}
            >Bay latency settle time</legend
        >
        <div
            class="option"
            role="group"
            use:hinted={range(settleRange)}
            oncontextmenu={(event) =>
                openSettingMenu(
                    event,
                    "bayLatencySettleMs",
                    typedNumber(
                        event.currentTarget.querySelector("input"),
                        "bayLatencySettleMs",
                    ),
                )}
        >
            <input
                id="latency-settle"
                class="number"
                type="number"
                aria-label="Bay latency settle time in milliseconds"
                min={settleRange.min}
                max={settleRange.max}
                step="1"
                value={editor.settings.bayLatencySettleMs}
                onchange={(event) =>
                    changeWhole(event, "bayLatencySettleMs", settleRange)}
            />
            <span>ms</span>
        </div>
    </fieldset>

    <fieldset>
        <legend
            class="h2"
            use:hinted={"After a bay leaves Bypass or Off, the audio sent to it stays silent this much longer before it fades in, so a plugin cleared through the relay's Tail cut output (which reacts some ms late) comes back on silence. The panel's own sound plays on until the bay's sound is back."}
            oncontextmenu={(event) =>
                openGroupMenu(event, groups.tailCutRelease)}
            >Bay tail cut release</legend
        >
        <div
            class="option"
            role="group"
            use:hinted={range(releaseRange)}
            oncontextmenu={(event) =>
                openSettingMenu(
                    event,
                    "bayTailCutReleaseMs",
                    typedNumber(
                        event.currentTarget.querySelector("input"),
                        "bayTailCutReleaseMs",
                    ),
                )}
        >
            <input
                id="tail-cut-release"
                class="number"
                type="number"
                aria-label="Bay tail cut release in milliseconds"
                min={releaseRange.min}
                max={releaseRange.max}
                step="1"
                value={editor.settings.bayTailCutReleaseMs}
                onchange={(event) =>
                    changeWhole(event, "bayTailCutReleaseMs", releaseRange)}
            />
            <span>ms</span>
        </div>
    </fieldset>
</div>

<style>
    p {
        margin: 0 0 8px;
    }

    .version,
    .note {
        color: var(--mid-gray);
    }

    fieldset {
        margin: 10px 0 0;
        padding: 0;
        border: none;
    }

    legend {
        padding: 0 0 2px;
    }

    /* The control in its own column, level with the text's first line; only the control reacts to clicks */
    .option {
        display: grid;
        grid-template-columns: auto 1fr;
        align-items: start;
        column-gap: 6px;
        padding: 1px 0;
        line-height: 21px;
    }

    input[type="checkbox"],
    input[type="radio"] {
        margin-top: 4px;
    }

    /* One line tall, so it sits level with the text beside it */
    .number {
        box-sizing: border-box;
        width: 4em;
        height: 21px;
    }

    /* Fields inside a line of text */
    .fields {
        display: flex;
        align-items: center;
        gap: 6px;
        padding: 1px 0;
    }

    /* Option labels; the values typed into fields stay white */
    .option > span,
    .fields > span {
        color: var(--mid-gray);
    }

    .warning {
        margin: 4px 0 0;
        color: var(--warning);
    }

    .line {
        display: flex;
        align-items: center;
        gap: 8px;
    }

    .size {
        display: flex;
        align-items: center;
        justify-content: space-between;
        margin-top: 14px;
    }

    .buttons {
        display: flex;
        gap: 8px;
    }

    /* Something more on the right-click */
    .more {
        cursor: help;
    }

    .group {
        margin: 14px 0;
        padding: 4px 0 12px;
        border-top: 1px solid rgba(255, 255, 255, 0.3);
        border-bottom: 1px solid rgba(255, 255, 255, 0.3);
    }
</style>
