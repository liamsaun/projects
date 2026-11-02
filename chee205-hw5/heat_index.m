%% CHEE 205 - Homework 5: Heat Index in MATLAB
% This script calculates the heat index ("apparent temperature") from an
% air temperature and a relative humidity entered by the user. It uses the
% Weather Prediction Center (WPC) / National Weather Service method:
%
%   1. Try the simple Steadman formula first.
%   2. If the average of that result and the temperature is 80 deg F or
%      higher, use the full Rothfusz regression equation instead.
%   3. Apply an adjustment factor for very dry or very humid conditions
%      when the Rothfusz equation is used.
%
% Reference: https://www.wpc.ncep.noaa.gov/html/heatindex_equation.shtml
%
% Inputs  (asked from the user): temperature units, temperature, relative humidity (%)
% Outputs (printed to screen):   heat index in deg F and deg C, two decimal places

clear; clc;

%% Step 1: Ask the user which temperature units they are using
% Keep asking until the user types F or C (upper or lower case is fine).
units = '';
while ~any(strcmpi(units, {'F', 'C'}))
    units = strtrim(input('Enter the temperature units (F for Fahrenheit, C for Celsius): ', 's'));
    if ~any(strcmpi(units, {'F', 'C'}))
        disp('  Invalid choice. Please type F or C.');
    end
end
units = upper(units);   % store as an upper-case letter for the checks below

%% Step 2: Ask the user for the air temperature
T_in = input(['Enter the air temperature in degrees ', units, ': ']);

%% Step 3: Ask the user for relative humidity (as a PERCENT, not a fraction)
disp('Relative humidity must be entered as a PERCENT from 0 to 100.');
disp('  Example: type 55 for 55% relative humidity (do NOT type 0.55).');
RH = input('Enter the relative humidity in percent: ');

% Check that the humidity is physically reasonable
while RH < 0 || RH > 100
    disp('  Relative humidity must be between 0 and 100 percent.');
    RH = input('Enter the relative humidity in percent: ');
end

%% Step 4: Convert the temperature to Fahrenheit if it was entered in Celsius
% The WPC equation is written for temperature in deg F.
if strcmp(units, 'C')
    T = T_in * 9/5 + 32;        % deg C -> deg F
else
    T = T_in;                   % already in deg F
end

%% Step 5: Calculate the heat index with the simple (Steadman) formula
HI_simple = 0.5 * (T + 61.0 + (T - 68.0) * 1.2 + RH * 0.094);

%% Step 6: Decide whether the full Rothfusz regression is needed
% WPC rule: if the average of HI_simple and T is 80 deg F or higher,
% the simple formula is not accurate enough, so use the regression.
adjustment = 0;   % default: no adjustment factor

if (HI_simple + T) / 2 < 80
    % Simple formula is good enough
    HI = HI_simple;
    method = 'Simple (Steadman) formula';
else
    % Full Rothfusz regression equation (T in deg F, RH in percent)
    HI = -42.379 + 2.04901523*T + 10.14333127*RH ...
         - 0.22475541*T*RH - 0.00683783*T^2 ...
         - 0.05481717*RH^2 + 0.00122874*T^2*RH ...
         + 0.00085282*T*RH^2 - 0.00000199*T^2*RH^2;
    method = 'Rothfusz regression';

    %% Step 7: Apply an adjustment factor if conditions require it
    if RH < 13 && T >= 80 && T <= 112
        % Low humidity adjustment (this amount is SUBTRACTED)
        adjustment = -((13 - RH) / 4) * sqrt((17 - abs(T - 95)) / 17);
        method = [method, ' + low-humidity adjustment'];
    elseif RH > 85 && T >= 80 && T <= 87
        % High humidity adjustment (this amount is ADDED)
        adjustment = ((RH - 85) / 10) * ((87 - T) / 5);
        method = [method, ' + high-humidity adjustment'];
    end

    HI = HI + adjustment;
end

%% Step 8: Convert the final heat index to Celsius
HI_F = HI;
HI_C = (HI_F - 32) * 5/9;

%% Step 9: Display the results
fprintf('\n----------------------- Results -----------------------\n');
fprintf('Input temperature:     %.2f deg %s (%.2f deg F)\n', T_in, units, T);
fprintf('Relative humidity:     %.2f %%\n', RH);
fprintf('Method used:           %s\n', method);
fprintf('Adjustment factor:     %.2f deg F\n', adjustment);
% Required output: both final values on ONE line with TWO decimal places
fprintf('Heat Index = %.2f deg F  |  %.2f deg C\n', HI_F, HI_C);
fprintf('-------------------------------------------------------\n');
