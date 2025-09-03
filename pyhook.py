import numpy as np
import pandas as pd

# momentum signal
# when called from C++ std::vector<double> x_ comes as a list
def momentum(x_:list, fast:int=10, slow:int=5, vol_look_back:int=5, min_periods:int=5, adjust:bool=False) -> np.float32:
    x = pd.Series(x_) 
    cross_over = x.ewm(span=fast,min_periods=min_periods,adjust=adjust).mean() \
                 - x.ewm(span=slow,min_periods=min_periods,adjust=adjust).mean()
    vol = x.ewm(span=vol_look_back,min_periods=min_periods,adjust=adjust).std()
    sig = cross_over/vol
    #print(sig.iloc[-1])
    return sig.iloc[-1]
