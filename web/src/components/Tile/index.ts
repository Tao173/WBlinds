import { _Component, Component } from "../Component";
import template from "./Tile.html";
import "./Tile.css";

type ClickHandler = (data: TileProps) => unknown;
export interface TileAPI {
  onClick: (handler: ClickHandler) => void;
  destroy(): void;
}

interface TileProps {
  name: string;
  id: string;
}

const _Tile: Component<TileAPI, TileProps> = function ({
  name,
  id,
}: TileProps) {
  let _clickHandlers: ClickHandler[] = [];

  this.init = function (elem: HTMLElement) {
    elem.id = id;
    elem.onclick = (e) => {
      _clickHandlers.forEach((h) => h({ id, name }));
    };
    elem.querySelector("p").innerText = name;
    return {
      onClick: (h) => {
        _clickHandlers.push(h);
      },
      destroy: () => {
        _clickHandlers = [];
      },
    };
  };
  return template;
};

export type Tile = _Component<TileAPI>;
export const Tile = _Component(_Tile);