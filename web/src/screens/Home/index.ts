import { appendChild, createDiv, getElement, nextTick } from "../../util";
import { ComponentFunction, Component } from "../../components/Component";
import template from "./Home.html";
import { State } from "../../state";
import { Tile } from "../../components";
import "./Home.css";

type DeviceClickHandler = (device: any) => void;
export interface HomeAPI {
  onDeviceClick: (handler: DeviceClickHandler) => void;
  destroy(): void;
}

const DEVICE_TILE = "device";
const PRESET_TILE = "preset";

const _Home: ComponentFunction<HomeAPI> = function () {
  let _loading = true;
  let _tiles: Tile[] = [];
  let _deviceClickHandlers: DeviceClickHandler[] = [];

  this.init = function (elem: HTMLElement) {
    // initially spinner is showing,
    // rest is hidden in a div

    function loaded() {
      if (!_loading) return;
      const spinner = getElement("hl");
      const content = getElement("hlc");
      spinner.style.display = "none";
      content.classList.remove("hide");
      _loading = false;
    }

    function getAllTiles(type: "preset" | "device"): {
      container: HTMLElement;
      tiles: NodeListOf<HTMLDivElement>;
    } {
      const container = getElement(`${type}-tiles`);
      return { container, tiles: container.querySelectorAll("div") };
    }

    function padTiles(type: "preset" | "device") {
      const { container, tiles } = getAllTiles(type);
      const w = container.clientWidth;
      const perRow = Math.floor(w / 110);
      console.log("client width: ", w);
      console.log("perRow: ", perRow);
      let len = tiles.length;
      while (len % perRow !== 0) {
        const e = createDiv();
        e.classList.add("tile", "sq", "em");
        appendChild(container, e);
        len++;
      }
    }

    function updateTiles(type: "preset" | "device", o: any) {
      const { container, tiles } = getAllTiles(type);
      tiles.forEach((tile) => {
        if (!(tile.id in o)) {
          // Existing, but doesn't exist in state
          // TODO: Remove component
        } else {
          // Exists, remove from list so it isn't added again
          o[tile.id] = undefined;
        }
      });

      for (const [k, v] of Object.entries(o)) {
        if (!v) continue;
        const t = Tile({
          id: `tile-${k}`,
          name: (v as any).name || k,
          ...(v as any),
        });
        t.onClick((data) => handleTileClick(type, data));
        _tiles.push(t);
        container.appendChild(t.node);
      }
      padTiles(type);
    }

    function handleTileClick(type: "device" | "preset", data: any) {
      if (type === "device") {
        _deviceClickHandlers.forEach((h) => h(data));
      } else {
        // TODO: handle preset click
      }
    }

    nextTick(() => {
      State.observe((PRESET_TILE + "s") as "presets", ({ value, prev }) => {
        console.log("presets updated: ", value, prev);
        loaded();

        updateTiles(PRESET_TILE, value);
      });

      State.observe((DEVICE_TILE + "s") as "devices", ({ value, prev }) => {
        console.log("devices updated: ", value, prev);
        loaded();

        updateTiles(DEVICE_TILE, value);
      });
    });

    return {
      onDeviceClick: (h: DeviceClickHandler) => {
        _deviceClickHandlers.push(h);
      },
      destroy: () => {
        _deviceClickHandlers = [];
        _tiles.forEach((t) => t.destroy());
        _tiles = [];
      },
    };
  };
  return template;
};

export type Home = Component<HomeAPI>;
export const Home = Component(_Home);