# Shader changes for D2R_DepthFog.fx (flat mode, monitor)

Edits to `D2R_DepthFog.fx` as released with D2R VR 0.185 (the file in `mods\D2R-VR\reshade\reshade-shaders\Shaders`, 208 693 bytes).
Three independent edits, each with the problem, the code before and after, and how it was tested.
Line numbers are those of that file. The public repository only has the 0.175 effect, so they may be a few lines off there.

Tested on a monitor (`[mode] platform=0`, flat F2/F3) with our own build of the plugin (0.171 sources) running on top
of the 0.185 mod, `[fog] spare_effects=0`. Not tested with the released 0.185 plugin: the first two edits are shader
only, the third needs the plugin to set one uniform (see there).

## 1. Effects drawn without depth are erased on the horizon

**Problem:** a shrine mark, a spell or a hand drawn without depth takes the depth of the far ground behind it. On the
horizon the fog amount there is 1, so the mix leaves only fog colour and the effect is cut off.

**Edit:** at the end of `DepthFogWorld` (line 1692, after the cave-halo `return`), put the light back on top of the
fog, the brighter the pixel.

Before:

```
    return lerp(colour, fogColour, t);
}
```

After:

```
    // A shrine mark, a spell or a hand drawn without depth sits on the far ground's
    // depth. On the horizon that fog amount is 1, so the lerp would leave only fog colour.
    // The light is put back on top, the brighter the pixel (the same 0.45..0.85 as over the
    // void). Dim ground, leaves and grass stay fogged.
    const float light = smoothstep(0.45, 0.85, max(colour.r, max(colour.g, colour.b)));
    return lerp(colour, fogColour, t) + colour * (t * light);
}
```

**Tested:** spells at the horizon keep their light. Very bright foam on far water can come back on that strip.

## 2. The same for pixels over the empty depth buffer (the void)

**Problem:** over the void (sky, far horizon) the fog amount `tf` is taken from the first drawn surface a few pixels
below, which is fully fogged on the horizon. That cut the light of bright effects there even with edit 1.

**Edit:** in the same function, in the void branch, just before the line that builds `shown` (line ~1632, the one
that starts `// ([render] fog_in_frame: ...`), lower `tf` for bright pixels.

Before:

```
            // ([render] fog_in_frame: the effects' light at full, as the frame leaves it over the planks - FrameVoidFog)
            const float3 shown = sky + dither + colour * (seen * (1.0 - (FogInFrame ? 0.0 : tf)) * keep);
```

After:

```
            // A shrine mark or a spell over the void is bright and has no depth. The far ground
            // a few pixels below is fully fogged, which cut that light off on the horizon.
            // The brighter the pixel, the less of that fog it takes; dim things drawn the same way
            // (far leaves, grass) still take it (same 0.45..0.85 band as below).
            tf = lerp(tf, 0.0, smoothstep(0.45, 0.85, max(colour.r, max(colour.g, colour.b))));
            // ([render] fog_in_frame: the effects' light at full, as the frame leaves it over the planks - FrameVoidFog)
            const float3 shown = sky + dither + colour * (seen * (1.0 - (FogInFrame ? 0.0 : tf)) * keep);
```

**Tested:** with edits 1 and 2 the spells at the horizon are whole. With edit 1 alone part of them was still cut.

**`[fog] spare_effects`:** the effects are kept without it. With `spare_effects=1` the game lagged badly in Act 2 on
the lowered level every time it was on (switched on, off, and on again in the same place; off: no lag, effects fine). Turning it on needs
a game restart (the plugin hooks the game's pipelines at start); off works live.

## 3. Panels half transparent when the sky and fog stay on over a menu

**Problem:** with the sky and fog kept on while a panel is open (monitor), the panels look washed out: the interface
layer's alpha lets the sky show through the panel's own transparency.

**Edit:** add a uniform next to `UiMaskOn` (line 569) and use it in `DepthFog` (line ~1700).

Add after `uniform bool UiMaskOn < hidden = true; > = false;`:

```
// A menu on the monitor: the panel is already drawn over the world. Any pixel it covers keeps that
// colour whole, or the sky shows through the panel's own transparency.
uniform bool UiMaskSolid < hidden = true; > = false;
```

Before:

```
    const float ui = 1.0 - saturate(tex2Dlod(GameLayerSmp, float4(uv, 0, 0)).a);
    return lerp(fogged, tex2D(FogColorSmp, uv).rgb, ui);
```

After:

```
    float through = saturate(tex2Dlod(GameLayerSmp, float4(uv, 0, 0)).a);
    if (UiMaskSolid && through < 0.98) through = 0.0;
    return lerp(fogged, tex2D(FogColorSmp, uv).rgb, 1.0 - through);
```

**Plugin side:** `UiMaskSolid` must be set by the plugin, only while a menu is open on the monitor (`vr/vrcam.cpp`,
`SetUiMask`), and the interface layer has to be copied during a menu too (`vr/hud.cpp`). Without that the uniform stays
false and nothing changes. The sky and fog also have to stay on while a panel is open (`EffectsPaused` in `vr/vrcam.cpp`);
see section 2 and 3 of `CHANGES.md`.

**Tested:** panels solid, sky and fog in the gaps between them, on the 0.171-based build. Not re-checked on the 0.185
effect with the released plugin.
