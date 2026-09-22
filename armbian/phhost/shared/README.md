# Schwung shared modules

`constants.mjs` and `input_filter.mjs` are copied unmodified from Schwung 0.9.18
(`/data/UserData/move-anything/shared/`, github.com/charlesvestal/move-anything),
MIT, © Charles Vestal — see `LICENSE`.

Every appliance ui.js imports them by their Schwung path. phhost is the Schwung
stand-in, so it provides them itself: `shared-resolve.mjs` maps that path onto
this directory. Nothing of Schwung has to be installed on the Move.
