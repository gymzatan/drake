from __future__ import print_function

import math
import numpy as np
import unittest

from pydrake.examples.pendulum import PendulumPlant
from pydrake.trajectories import PiecewisePolynomial
from pydrake.systems.primitives import LinearSystem
from pydrake.systems.trajectory_optimization import (
    DirectCollocation, DirectTranscription
)


class TestTrajectoryOptimization(unittest.TestCase):
    def test_direct_collocation(self):
        plant = PendulumPlant()
        context = plant.CreateDefaultContext()

        kNumTimeSamples = 21
        kMinimumTimeStep = 0.2
        kMaximumTimeStep = 0.5
        dircol = DirectCollocation(plant, context, kNumTimeSamples,
                                   kMinimumTimeStep, kMaximumTimeStep)

        # Spell out most of the methods, regardless of whether they make sense
        # as a consistent optimization.  The goal is to check the bindings,
        # not the implementation.
        t = dircol.time()
        dt = dircol.timestep(0)
        x = dircol.state()
        x2 = dircol.state(2)
        x0 = dircol.initial_state()
        xf = dircol.final_state()
        u = dircol.input()
        u2 = dircol.input(2)

        dircol.AddRunningCost(x.dot(x))
        dircol.AddConstraintToAllKnotPoints(u[0] == 0)
        dircol.AddTimeIntervalBounds(0.3, 0.4)
        dircol.AddEqualTimeIntervalsConstraints()
        dircol.AddDurationBounds(.3*21, 0.4*21)
        dircol.AddFinalCost(2*x.dot(x))

        initial_u = PiecewisePolynomial.ZeroOrderHold([0, .3*21], [[0], [0]])
        initial_x = PiecewisePolynomial()
        dircol.SetInitialTrajectory(initial_u, initial_x)

        dircol.Solve()

        times = dircol.GetSampleTimes()
        inputs = dircol.GetInputSamples()
        states = dircol.GetStateSamples()
        input_traj = dircol.ReconstructInputTrajectory()
        state_traj = dircol.ReconstructStateTrajectory()

    def test_direct_transcription(self):
        # Integrator.
        plant = LinearSystem(A=[0.], B=[1.], C=[1.], D=[0.], time_period=0.1)
        context = plant.CreateDefaultContext()

        kNumTimeSamples = 21
        dirtran = DirectTranscription(plant, context, kNumTimeSamples)

        # Spell out most of the methods, regardless of whether they make sense
        # as a consistent optimization.  The goal is to check the bindings,
        # not the implementation.
        t = dirtran.time()
        dt = dirtran.fixed_timestep()
        x = dirtran.state()
        x2 = dirtran.state(2)
        x0 = dirtran.initial_state()
        xf = dirtran.final_state()
        u = dirtran.input()
        u2 = dirtran.input(2)

        dirtran.AddRunningCost(x.dot(x))
        dirtran.AddConstraintToAllKnotPoints(u[0] == 0)
        dirtran.AddFinalCost(2*x.dot(x))

        initial_u = PiecewisePolynomial.ZeroOrderHold([0, .3*21], [[0], [0]])
        initial_x = PiecewisePolynomial()
        dirtran.SetInitialTrajectory(initial_u, initial_x)

        dirtran.Solve()

        times = dirtran.GetSampleTimes()
        inputs = dirtran.GetInputSamples()
        states = dirtran.GetStateSamples()
        input_traj = dirtran.ReconstructInputTrajectory()
        state_traj = dirtran.ReconstructStateTrajectory()
