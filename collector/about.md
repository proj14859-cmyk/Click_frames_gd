# Click Frames Collector

Collects normal-mode attempts locally for future model training. It requires
Click Frames GD and never sends data over the network.

Data is separated into `normal-attempts/` and `completed-runs/` inside this
mod's save directory. Each attempt contains metadata, inputs and trajectory.
