%% CHEE 205 Homework 8 - Part 1: ODE45 with One Equation
% Liam Saunders and Spencer Hunt
% First-order reaction in a batch reactor: d[A]/dt = -k[A]
clear; clc; close all;

A0 = 100;                   % initial concentration of A (mol/L)
tspan = [0 15];             % time span (hr)
kValues = [0.2 0.1 0.05];   % rate constants (1/hr)

figure; hold on
for i = 1:length(kValues)
    k = kValues(i);                     % current rate constant (1/hr)
    dAdt = @(t, A) -k*A;                % one ODE written as a function handle
    [t, A] = ode45(dAdt, tspan, A0);    % solve d[A]/dt = -k[A] with ode45
    plot(t, A, 'LineWidth', 1.5)        % plot [A] vs. time for this k
end
hold off
title('Part 1: First-Order Batch Reactor, [A] vs. Time')
xlabel('Time (hr)')
ylabel('[A] (mol/L)')
legend('k = 0.2 hr^{-1}', 'k = 0.1 hr^{-1}', 'k = 0.05 hr^{-1}')
grid on

%% Part 2: ODE45 with More Than One Equation
% Series reactions in a batch reactor: A -> B (k1), B -> C (k2)
k1 = 0.2;                   % rate constant for A -> B (1/hr)
k2 = 0.1;                   % rate constant for B -> C (1/hr)
y0 = [1.0; 0; 0];           % initial [A], [B], [C] (mol/L)
tspan2 = [0 10];            % time span (hr)

% Solve the system: y(:,1) = [A], y(:,2) = [B], y(:,3) = [C]
[t2, y] = ode45(@(t, y) reactionNetwork(t, y, k1, k2), tspan2, y0);

figure
plot(t2, y(:,1), t2, y(:,2), t2, y(:,3), 'LineWidth', 1.5)
title('Part 2: A \rightarrow B \rightarrow C Batch Reactor')
xlabel('Time (hr)')
ylabel('Concentration (mol/L)')
legend('[A]', '[B]', '[C]')
grid on

%% Local function for the Part 2 ODE system
function dydt = reactionNetwork(~, y, k1, k2)
    % One derivative per species: y(1)=[A], y(2)=[B], y(3)=[C]
    dydt = zeros(3,1);
    dydt(1) = -k1*y(1);             % d[A]/dt
    dydt(2) =  k1*y(1) - k2*y(2);   % d[B]/dt
    dydt(3) =  k2*y(2);             % d[C]/dt
end
