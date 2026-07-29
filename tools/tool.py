#!/usr/bin/env python3
"""Extraordinary signals — DearPyGui 2.3.0, Python 3.14+."""

from __future__ import annotations

import logging
import math
import sys
from time import perf_counter
from typing import ClassVar, Final

import dearpygui.dearpygui as dpg


def _logger(name: str = "tool") -> logging.Logger:
    log = logging.getLogger(name)
    if not log.handlers:
        log.setLevel(logging.INFO)
        h = logging.StreamHandler(sys.stdout)
        h.setFormatter(
            logging.Formatter("%(asctime)s | %(levelname)s | %(message)s", "%H:%M:%S")
        )
        log.addHandler(h)
    return log


class App:
    _SCALE: ClassVar[float] = 1.5
    _W: ClassVar[int] = 1280
    _H: ClassVar[int] = 720
    _N: ClassVar[int] = 500

    def __init__(self) -> None:
        self._log: Final = _logger()
        self._t0: Final[float] = perf_counter()
        self._xs: Final[list[float]] = [i * 0.06 for i in range(self._N)]
        self._we: Final[list[tuple[float, float, float]]] = [
            (0.6**n, 3.0**n, float(n)) for n in range(6)
        ]

        self._sa: str | int = ""
        self._sb: str | int = ""
        self._sc: str | int = ""
        self._status: str | int = ""

    def _tick(self, _s: object = None, _a: object = None) -> None:
        t = perf_counter() - self._t0
        xs = self._xs

        ya = []
        x0 = 5.0 + 0.5 * t
        sigma = 1.0 + 0.12 * t
        k0 = 5.0
        chirp = 0.025 * t
        for x in xs:
            dx = x - x0
            env = math.exp(-0.5 * (dx / sigma) ** 2)
            phase = k0 * dx + chirp * dx * dx
            ya.append(env * math.cos(phase))

        yb = []
        wt = 0.2 * t
        for x in xs:
            s = 0.0
            for amp, freq, off in self._we:
                s += amp * math.cos(freq * (x + wt) + off)
            yb.append(s)

        yc = []
        center = 20.0
        sin_t = math.sin(0.8 * t)
        for x in xs:
            yc.append(4.0 * math.atan(sin_t / math.cosh(0.4 * (x - center))))

        dpg.set_value(self._sa, [xs, ya])
        dpg.set_value(self._sb, [xs, yb])
        dpg.set_value(self._sc, [xs, yc])

        flat = ya + yb + yc
        lo = min(flat)
        hi = max(flat)
        pad = (hi - lo) * 0.12 + 0.05
        dpg.set_axis_limits("y_axis", lo - pad, hi + pad)

        dpg.set_value(self._status, f"t={t:.2f}s")
        dpg.set_value("fps_text", f"FPS: {dpg.get_frame_rate():.0f}")

        dpg.set_frame_callback(dpg.get_frame_count() + 1, self._tick)

    def _build(self) -> None:
        dpg.set_global_font_scale(self._SCALE)

        with dpg.viewport_menu_bar():
            with dpg.menu(label="File"):
                dpg.add_menu_item(label="Quit", callback=lambda: dpg.stop_dearpygui())
            with dpg.menu(label="View"):
                dpg.add_menu_item(label="Metrics", callback=lambda: dpg.show_metrics())
                dpg.add_menu_item(
                    label="Style Editor", callback=lambda: dpg.show_style_editor()
                )

        with dpg.window(
            label="Main",
            tag="main",
            no_title_bar=True,
            no_move=True,
            no_resize=True,
            no_collapse=True,
            no_scrollbar=True,
        ):
            dpg.add_spacer(height=10)
            dpg.add_text("CMake Tool  ·  Python 3.14 + DearPyGui 2.3.0")
            dpg.add_separator()

            with dpg.plot(label="", height=-60, width=-1):
                dpg.add_plot_legend()

                dpg.add_plot_axis(dpg.mvXAxis, tag="x_axis")
                dpg.add_plot_axis(dpg.mvYAxis, tag="y_axis")

                self._sa = dpg.add_line_series(
                    self._xs, [0.0] * self._N, label="quantum packet", parent="y_axis"
                )
                self._sb = dpg.add_line_series(
                    self._xs,
                    [0.0] * self._N,
                    label="weierstrass fractal",
                    parent="y_axis",
                )
                self._sc = dpg.add_line_series(
                    self._xs,
                    [0.0] * self._N,
                    label="sine-gordon breather",
                    parent="y_axis",
                )

            dpg.set_axis_limits("x_axis", self._xs[0], self._xs[-1])

            with dpg.group(horizontal=True):
                self._status = dpg.add_text("t=0.00s")
                dpg.add_spacer(width=600)
                dpg.add_text("FPS: --", tag="fps_text")

        dpg.set_primary_window("main", True)
        dpg.set_frame_callback(2, self._tick)

    def run(self) -> int:
        self._log.info("start")
        dpg.create_context()
        try:
            dpg.create_viewport(title="CMake Tool", width=self._W, height=self._H)
            self._build()
            dpg.setup_dearpygui()
            dpg.show_viewport()
            self._log.info("run")
            dpg.start_dearpygui()
        except Exception:
            self._log.exception("fatal")
            return 1
        finally:
            dpg.destroy_context()
        self._log.info("stop")
        return 0


def main() -> int:
    return App().run()


if __name__ == "__main__":
    sys.exit(main())
