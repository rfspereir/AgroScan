import { Component } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Router } from '@angular/router';

import { Auth, signInWithEmailAndPassword } from '@angular/fire/auth';

@Component({
  selector: 'app-login',
  standalone: true,
  imports: [CommonModule, FormsModule],
  templateUrl: './login.html',
  styleUrls: ['./login.css']
})
export class Login {
  email = '';
  password = '';
  errorMessage = '';

  constructor(private auth: Auth, private router: Router) {}

  login() {
    this.errorMessage = '';

    signInWithEmailAndPassword(this.auth, this.email.trim(), this.password.trim())
      .then(() => {
        this.router.navigate(['/dashboard-root']);
      })
      .catch((error) => {
        console.error('Erro no login:', error);
        this.errorMessage = 'E-mail ou senha inválidos.';
      });
  }
}
