<script lang="ts">
    import { sendResize, type Corner } from "../lib/bridge";

    // The web view covers the plugin's own handles at the bottom corners, so the page provides those
    const corners: Corner[] = ["bottomLeft", "bottomRight"];

    function grip(node: HTMLElement, corner: Corner) {
        let dragging = false;
        let movePending = false;

        const start = (event: PointerEvent) => {
            if (event.button !== 0) return;

            event.preventDefault();
            dragging = true;
            sendResize(corner, "start");

            try {
                node.setPointerCapture(event.pointerId);
            } catch {
                // The pointer is already gone; its release still ends the drag
            }
        };

        // The plugin follows the pointer itself, so one message per frame is plenty
        const move = () => {
            if (!dragging || movePending) return;

            movePending = true;
            requestAnimationFrame(() => {
                movePending = false;

                if (dragging) sendResize(corner, "move");
            });
        };

        const end = () => {
            if (!dragging) return;

            dragging = false;
            sendResize(corner, "end");
        };

        const events = [
            ["pointerdown", start],
            ["pointermove", move],
            ["pointerup", end],
            ["pointercancel", end],
            ["lostpointercapture", end],
        ] as const;

        for (const [type, listener] of events)
            node.addEventListener(type, listener as EventListener);

        return {
            destroy() {
                end();

                for (const [type, listener] of events)
                    node.removeEventListener(type, listener as EventListener);
            },
        };
    }
</script>

{#each corners as corner (corner)}
    <div class="grip {corner}" use:grip={corner}>
        <svg viewBox="0 0 16 16" aria-hidden="true">
            <path d="M 3 7.5 L 8.5 13 M 3 2.5 L 13.5 13" />
        </svg>
    </div>
{/each}

<style>
    .grip {
        position: fixed;
        bottom: 0;
        width: 16px;
        height: 16px;
        z-index: 50;
        touch-action: none;
    }

    .bottomLeft {
        left: 0;
        cursor: nesw-resize;
    }

    .bottomRight {
        right: 0;
        cursor: nwse-resize;
    }

    .bottomRight svg {
        transform: scaleX(-1);
    }

    svg {
        display: block;
        width: 100%;
        height: 100%;
        stroke: rgba(255, 255, 255, 0.35);
        stroke-width: 1;
        fill: none;
    }
</style>
