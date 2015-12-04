# Adding Session Checkout Exception Error Responses

If you add a new failure mode to checking out a session (for example a request timing out in the queue), then you need to add the Exception to the list of casts in `void Controller::reportSessionCheckoutError(Client *client, Request *req, const ExceptionPtr &e);`. Otherwise the default Exception Response will be used.
